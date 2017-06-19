#ifndef RUN_STRIDE_H_INCLUDED
#define RUN_STRIDE_H_INCLUDED

#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "core/Cluster.h"
#include "multiregion/TravelModel.h"
#include "output/VisualizerData.h"
#include "pop/Population.h"
#include "sim/SimulationConfig.h"
#include "sim/Simulator.h"
#include "util/Stopwatch.h"

namespace stride {

/**
 * Defines the result of a single Stride simulation.
 */
class StrideSimulatorResult final
{
public:
	StrideSimulatorResult(multiregion::RegionId id, bool generate_vis)
	    : id(id), generate_vis_data(generate_vis), run_clock("run_clock", false), day()
	{
	}

	const multiregion::RegionId id;
	std::vector<unsigned int> cases;
	VisualizerData visualizer_data;
	bool generate_vis_data;

	/// Gets the total run-time for this simulator result.
	util::Stopwatch<>::TDuration GetRuntime() const { return run_clock.Get(); }

	/// Gets the total run-time for this simulator result, as a string.
	std::string GetRuntimeString() const { return run_clock.ToString(); }

	/// Performs an action just before a simulator step is performed.
	void BeforeSimulatorStep(Simulator& simulator);

	/// Performs an action just after a simulator step has been performed.
	void AfterSimulatorStep(Simulator& simulator);

private:
	util::Stopwatch<> run_clock;
	int day;
	static std::mutex io_mutex;
};

/// Prints and returns the number of threads for parallelization.
unsigned int print_number_of_threads();

/// Prints information about the current execution environment.
void print_execution_environment();

/// Verifies that Stride is being run in the right execution environment.
void verify_execution_environment();

/// Runs the simulator with the given multi-simulation configuration.
void run_stride(const MultiSimulationConfig& config);

/// Runs the simulator with the given single-simulation configuration.
void run_stride(const SingleSimulationConfig& config);

/// Runs the simulator with the given configuration file.
void run_stride(
    bool track_index_case, const std::string& config_file_name, const std::string& h5_file, const std::string& date,
    bool gen_vis = false, bool checkpoint = false, unsigned int interval = -1);

/// Runs the simulator if no config file was given. It will try to load the h5_file.
void run_stride_noConfig(
    bool track_index_case, const std::string& h5_file, const std::string& date, bool gen_vis, unsigned int interval);

} // end_of_namespace

#endif // end-of-include-guard
