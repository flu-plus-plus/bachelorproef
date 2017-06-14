#include "run_stride.h"

#include "checkpoint/CheckPoint.h"
#include "multiregion/ParallelSimulationManager.h"
#include "multiregion/SimulationManager.h"
#include "multiregion/TravelModel.h"
#include "output/CasesFile.h"
#include "output/PersonFile.h"
#include "output/SummaryFile.h"
#include "output/VisualizerFile.h"
#include "sim/Simulator.h"
#include "sim/SimulatorBuilder.h"
#include "util/Errors.h"
#include "util/ExternalVars.h"
#include "util/InstallDirs.h"
#include "util/Parallel.h"
#include "util/Stopwatch.h"
#include "util/TimeStamp.h"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <spdlog/spdlog.h>

#include <cmath>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace stride {

using namespace output;
using namespace util;
using namespace boost::filesystem;
using namespace boost::property_tree;
using namespace std;
using namespace std::chrono;
using namespace checkpoint;

std::mutex StrideSimulatorResult::io_mutex;
bool load = false;
boost::gregorian::date date;

#if USE_HDF5
std::unique_ptr<CheckPoint> cp;
#endif

/// Performs an action just before a simulator step is performed.
void StrideSimulatorResult::BeforeSimulatorStep(Simulator& sim)
{
	run_clock.Start();

#if USE_HDF5
	if (sim.GetConfiguration().common_config->use_checkpoint) {
		if (day == 0 && !load) {
			// saves the start configuration
			cp->OpenFile();
			cp->SaveCheckPoint(sim, day);
			cp->WriteAtlas(sim.GetPopulation()->GetAtlas());
			cp->CloseFile();
		}
	}
#endif

	if (util::INTERRUPT) {
		exit(-1);
	}
}

/// Performs an action just after a simulator step has been performed.
void StrideSimulatorResult::AfterSimulatorStep(const Simulator& sim)
{
#if USE_HDF5
	if (sim.GetConfiguration().common_config->use_checkpoint) {
		// saves the last configuration or configuration after an interval.
		if (sim.IsDone() || util::INTERRUPT ||
		    (day + 1) % sim.GetConfiguration().common_config->checkpoint_interval == 0) {
			cp->OpenFile();
			cp->SaveCheckPoint(sim, day);
			cp->CloseFile();
		}
	}
#endif
	auto pop = sim.GetPopulation();
	run_clock.Stop();
	auto infected_count = sim.GetPopulation()->get_infected_count();
	cases.push_back(infected_count);

	if (generate_vis_data && pop->has_atlas)
		visualizer_data.AddDay(pop);

	day++;

	lock_guard<mutex> lock(io_mutex);
	cout << "Simulation " << setw(3) << id << ": simulated day: " << setw(5) << (day - 1)
	     << "     Done, infected count: " << setw(10) << infected_count << endl;

	if (util::INTERRUPT) {
		exit(-1);
	}
}

/// Prints and returns the number of threads.
unsigned int print_number_of_threads()
{
	unsigned int num_threads = stride::util::parallel::get_number_of_threads();
	if (stride::util::parallel::using_parallelization_library) {
		cout << "Using " << stride::util::parallel::parallelization_library_name << " threads: " << num_threads
		     << endl;
	} else {
		cout << "Not using threads for parallelization." << endl;
	}
	return num_threads;
}

/// Prints information about the current execution environment.
void print_execution_environment()
{
	cout << "\n*************************************************************" << endl;
	cout << "Starting up at:      " << TimeStamp().ToString() << endl;
	cout << "Executing:           " << InstallDirs::GetExecPath().string() << endl;
	cout << "Current directory:   " << InstallDirs::GetCurrentDir().string() << endl;
	cout << "Install directory:   " << InstallDirs::GetRootDir().string() << endl;
	cout << "Data    directory:   " << InstallDirs::GetDataDir().string() << endl;
}

/// Verifies that Stride is being run in the right execution environment.
void verify_execution_environment()
{
	if (InstallDirs::GetCurrentDir().compare(InstallDirs::GetRootDir()) != 0) {
		FATAL_ERROR("Current directory is not install root! Aborting.");
	}
	if (InstallDirs::GetDataDir().empty()) {
		FATAL_ERROR("Data directory not present! Aborting.");
	}
}

/// Run the stride simulator.
void run_stride(const MultiSimulationConfig& config)
{
	// -----------------------------------------------------------------------------------------
	// OpenMP.
	// -----------------------------------------------------------------------------------------
	unsigned int num_threads = print_number_of_threads();

	// -----------------------------------------------------------------------------------------
	// Set output path prefix.
	// -----------------------------------------------------------------------------------------

	if (config.log_config->output_prefix.length() == 0) {
		config.log_config->output_prefix = TimeStamp().ToTag();
	}
	auto output_prefix = config.log_config->output_prefix;

	cout << "Project output tag:  " << output_prefix << endl << endl;

	// -----------------------------------------------------------------------------------------
	// Track index case setting.
	// -----------------------------------------------------------------------------------------
	cout << "Setting for track_index_case:  " << boolalpha << config.common_config->track_index_case << endl;

	// Set the log queue size.
	spdlog::set_async_mode(1048576);

	// -----------------------------------------------------------------------------------------
	// Create simulator.
	// -----------------------------------------------------------------------------------------
	Stopwatch<> total_clock("total_clock", true);
	// multiregion::SequentialSimulationManager<StrideSimulatorResult, multiregion::RegionId> sim_manager{
	//     num_threads};
	multiregion::ParallelSimulationManager<StrideSimulatorResult, multiregion::RegionId> sim_manager{
	    config.region_models.size(), num_threads};

	// Build all the simulations.
	struct SimulationTuple
	{
		std::string log_name;
		std::string sim_output_prefix;
		SingleSimulationConfig sim_config;
		std::shared_ptr<multiregion::SimulationTask<StrideSimulatorResult>> sim_task;
	};
	std::vector<SimulationTuple> tasks;
	for (const auto& single_config : config.GetSingleConfigs()) {
		multiregion::RegionId region_id = single_config.GetId();
		cout << "Building simulator #" << region_id << endl;
		auto sim_output_prefix = output_prefix + "_sim" + std::to_string(region_id);

		// -----------------------------------------------------------------------------------------
		// Create logger
		// Transmissions:     [TRANSMISSION] <infecterID> <infectedID> <clusterID> <day>
		// General contacts:  [CNT] <person1ID> <person1AGE> <person2AGE>  <at_home> <at_work> <at_school>
		// <at_other>
		// -----------------------------------------------------------------------------------------
		auto log_name = std::string("contact_logger_") + sim_output_prefix;
		auto file_logger = spdlog::rotating_logger_mt(
		    log_name, sim_output_prefix + "_logfile", std::numeric_limits<size_t>::max(),
		    std::numeric_limits<size_t>::max());
		file_logger->set_pattern("%v"); // Remove meta data from log => time-stamp of logging

#if USE_HDF5
		if (load) {
			tasks.push_back(
			    {log_name, sim_output_prefix, single_config,
			     sim_manager.LoadSimulation(single_config, file_logger,cp,date ,region_id)});
		} else {
			tasks.push_back(
			    {log_name, sim_output_prefix, single_config,
			     sim_manager.CreateSimulation(single_config, file_logger, region_id)});
		}
#else
		tasks.push_back(
		    {log_name, sim_output_prefix, single_config,
		     sim_manager.CreateSimulation(single_config, file_logger, region_id)});
#endif
	}
	cout << "Done building simulators. " << endl << endl;

	// -----------------------------------------------------------------------------------------
	// Run the simulation.
	// -----------------------------------------------------------------------------------------
	Stopwatch<> sim_clock("sim_clock", true);
	sim_manager.WaitAll();
	sim_clock.Stop();

	// Generate output files for the simulations.
	for (const auto& sim_tuple : tasks) {
		// -----------------------------------------------------------------------------------------
		// Generate output files
		// -----------------------------------------------------------------------------------------
		// Cases
		auto sim_result = sim_tuple.sim_task->GetResult();
		auto pop = sim_tuple.sim_task->GetPopulation();
		CasesFile cases_file(sim_tuple.sim_output_prefix);
		cases_file.Print(sim_result.cases);

		// Summary
		SummaryFile summary_file(sim_tuple.sim_output_prefix);
		summary_file.Print(
		    sim_tuple.sim_config, sim_tuple.sim_task->GetPopulationSize(),
		    sim_tuple.sim_task->GetInfectedCount(),
		    duration_cast<milliseconds>(sim_result.GetRuntime()).count(),
		    duration_cast<milliseconds>(total_clock.Get()).count());

		// Persons
		if (sim_tuple.sim_config.log_config->generate_person_file) {
			PersonFile person_file(sim_tuple.sim_output_prefix);
			person_file.Print(pop);
		}

		// Visualization
		if (pop->has_atlas && sim_tuple.sim_config.common_config->generate_vis_file) {
			VisualizerFile vis_file(sim_tuple.sim_output_prefix);
			vis_file.Print(pop->GetAtlas().getTownMap(), sim_result.visualizer_data);
		}

		cout << "simulator #" << sim_result.id << " is done; simulation time: " << sim_result.GetRuntimeString()
		     << endl;

		spdlog::drop(sim_tuple.log_name);
	}

	// -----------------------------------------------------------------------------------------
	// Print final message to command line.
	// -----------------------------------------------------------------------------------------
	cout << endl << endl;
	cout << "total time: " << total_clock.ToString() << endl
	     << "total simulation time: " << sim_clock.ToString() << endl
	     << "Exiting at: " << TimeStamp().ToString() << endl
	     << endl;
}

/// Run the stride simulator.
void run_stride(const SingleSimulationConfig& config) { run_stride(config.AsMultiConfig()); }

/// Run the stride simulator.
void run_stride(
    bool track_index_case, const string& config_file_name, const std::string& h5_file, const std::string& date,
    bool gen_vis, bool checkpoint, unsigned int interval)
{
	if (config_file_name.empty() and checkpoint) {
		run_stride_noConfig(track_index_case, h5_file, date, gen_vis, interval);
		return;
	}
	std::string realFile(h5_file);
	// Parse the configuration.
	ptree pt_config;
	const auto file_path = canonical(system_complete(config_file_name));
	if (!is_regular_file(file_path)) {
		throw runtime_error(
		    string(__func__) + ">Config file " + file_path.string() + " not present. Aborting.");
	}
	read_xml(file_path.string(), pt_config);
	cout << "Configuration file:  " << file_path.string() << endl;

	MultiSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = track_index_case;
	config.common_config->generate_vis_file = gen_vis;
	config.common_config->use_checkpoint = checkpoint;
	config.common_config->checkpoint_interval = interval;

	if (config.log_config->output_prefix.length() == 0) {
		config.log_config->output_prefix = TimeStamp().ToTag();
	}
	if (h5_file.empty()) {
		realFile = TimeStamp().ToTag() + ".h5";
	}
#if USE_HDF5
	if (config.common_config->use_checkpoint) {
		cp = std::make_unique<CheckPoint>(realFile);

		cp->CreateFile();
		cp->OpenFile();
		cp->WriteConfig(config);
		cp->WriteHolidays(
		    pt_config.get_child("run").get<std::string>("holidays_file", "holidays_flanders_2016.json"));
		cp->CloseFile();
	}
#endif

	// Run Stride.
	run_stride(config);
}

void run_stride_noConfig(
    bool track_index_case, const std::string& h5_file, const std::string& datestr, bool gen_vis, unsigned int interval)
{
#if USE_HDF5
	load = true;
	std::string actualFile = h5_file;
	if (h5_file.empty()) {
		std::vector<boost::filesystem::path> hfiles;
		for (auto& i : boost::make_iterator_range(
			 boost::filesystem::directory_iterator(InstallDirs::GetCurrentDir()), {})) {
			if (boost::filesystem::is_regular_file(i) && i.path().extension() == ".h5") {
				hfiles.push_back(i.path());
			}
		}
		if (hfiles.empty()) {
			FATAL_ERROR("No valid h5 file found");
		}
		boost::filesystem::path besTime = hfiles[0];
		for (auto& p : hfiles) {
			if (boost::filesystem::last_write_time(besTime) < boost::filesystem::last_write_time(p)) {
				besTime = p;
			}
		}
		actualFile = besTime.filename().string();
	}
	cp = std::make_unique<CheckPoint>(actualFile);

	if (datestr.empty()) {
		cp->OpenFile();
		date = cp->GetLastDate();
		cp->CloseFile();
	} else {
		try {
			date = boost::gregorian::date(boost::gregorian::from_undelimited_string(datestr));
		} catch (std::exception& e) {
			date = boost::gregorian::date();
		}
	}

	if (date.is_not_a_date()) {
		FATAL_ERROR("No valid date found.");
	}
	cout << "Checkpoint file:  " << actualFile << endl;
	cout << "Date:  " << boost::gregorian::to_simple_string(date) << endl;

	cp->OpenFile();
	SingleSimulationConfig config = cp->LoadSingleConfig();
	config.common_config->initial_calendar = cp->LoadCalendar(date);
	cp->CloseFile();
	config.common_config->generate_vis_file = gen_vis;
	config.common_config->checkpoint_interval = interval;

	run_stride(config);

#else
	FATAL_ERROR("HDF5 NOT INSTALLED");
#endif
}

} // end_of_namespace
