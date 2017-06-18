#include "multiregion/Visitor.h"
#include "pop/Population.h"
#include "sim/Simulator.h"
#include "sim/SimulatorBuilder.h"
#include "util/Parallel.h"

#include <boost/property_tree/ptree.hpp>
#include <gtest/gtest.h>
#include <omp.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <map>
#include <string>
#include <tuple>

using namespace std;
using namespace stride;
using namespace stride::multiregion;
using namespace ::testing;

namespace Tests {

class BatchDemos : public ::testing::TestWithParam<tuple<string, unsigned int>>
{
public:
	/// TestCase set up.
	static void SetUpTestCase() {}

	/// Tearing down TestCase
	static void TearDownTestCase() {}

protected:
	/// Destructor has to be virtual.
	virtual ~BatchDemos() {}

	/// Set up for the test fixture
	virtual void SetUp() {}

	/// Tearing down the test fixture
	virtual void TearDown() {}

	// Data members of the test fixture
	static const string g_population_file;
	static const double g_r0;
	static const unsigned int g_num_days;
	static const unsigned int g_rng_seed;
	static const double g_seeding_rate;
	static const double g_immunity_rate;
	static const string g_disease_config_file;
	static const string g_output_prefix;
	static const string g_holidays_file;
	static const unsigned int g_num_participants_survey;
	static const string g_start_date;

	static const unsigned int g_rng_seed_adapted;
	static const double g_seeding_rate_adapted;
	static const double g_immunity_rate_adapted;
	static const string g_disease_config_file_adapted;
	static const double g_transmission_rate_measles;
	static const double g_transmission_rate_maximum;

	static const map<string, unsigned int> g_results;
};

// Default values
const string BatchDemos::g_population_file = "pop_oklahoma.csv";
const double BatchDemos::g_r0 = 3.0;
const unsigned int BatchDemos::g_num_days = 30U;
const unsigned int BatchDemos::g_rng_seed = 2015U;
const double BatchDemos::g_seeding_rate = 0.0001;
const double BatchDemos::g_immunity_rate = 0.0;
const string BatchDemos::g_disease_config_file = "disease_influenza.xml";
const string BatchDemos::g_output_prefix = "test";
const string BatchDemos::g_holidays_file = "holidays_none.json";
const unsigned int BatchDemos::g_num_participants_survey = 10;
const string BatchDemos::g_start_date = "2017-01-01";

// Adapted values
const double BatchDemos::g_seeding_rate_adapted = 0.0;
const double BatchDemos::g_immunity_rate_adapted = 0.999991;
const string BatchDemos::g_disease_config_file_adapted = "disease_measles.xml";
const double BatchDemos::g_transmission_rate_measles = 16U;
const double BatchDemos::g_transmission_rate_maximum = 100U;

const map<string, unsigned int> BatchDemos::g_results{make_pair("default", 75000), make_pair("seeding_rate", 0),
						      make_pair("immunity_rate", 6), make_pair("measles", 135000),
						      make_pair("maximum", 700000)};

TEST_P(BatchDemos, Run)
{
	// -----------------------------------------------------------------------------------------
	// Prepare test configuration.
	// -----------------------------------------------------------------------------------------
	tuple<string, unsigned int> t(GetParam());
	const string test_tag = get<0>(t);
	const unsigned int num_threads = get<1>(t);
	auto old_number_of_threads = stride::util::parallel::get_number_of_threads();
	if (!stride::util::parallel::try_set_number_of_threads(num_threads) && num_threads != 1) {
		return;
	}
	omp_set_schedule(omp_sched_static, 1);

	// -----------------------------------------------------------------------------------------
	// Setup configuration.
	// -----------------------------------------------------------------------------------------
	boost::property_tree::ptree pt_config;
	pt_config.put("run.rng_seed", g_rng_seed);
	pt_config.put("run.r0", g_r0);
	pt_config.put("run.seeding_rate", g_seeding_rate);
	pt_config.put("run.immunity_rate", g_immunity_rate);
	pt_config.put("run.population_file", g_population_file);
	pt_config.put("run.num_days", g_num_days);
	pt_config.put("run.output_prefix", g_output_prefix);
	pt_config.put("run.disease_config_file", g_disease_config_file);
	pt_config.put("run.num_participants_survey", g_num_participants_survey);
	pt_config.put("run.start_date", g_start_date);
	pt_config.put("run.holidays_file", g_holidays_file);
	pt_config.put("run.age_contact_matrix_file", "contact_matrix_average.xml");
	pt_config.put("run.log_level", "None");
	bool track_index_case = false;

	// -----------------------------------------------------------------------------------------
	// Override scenario settings.
	// -----------------------------------------------------------------------------------------
	if (test_tag == "default") {
		// do nothing
	}
	if (test_tag == "seeding_rate") {
		pt_config.put("run.seeding_rate", g_seeding_rate_adapted);
	}
	if (test_tag == "immunity_rate") {
		pt_config.put("run.seeding_rate", 1 - g_immunity_rate_adapted);
		pt_config.put("run.immunity_rate", g_immunity_rate_adapted);
	}
	if (test_tag == "measles") {
		pt_config.put("run.disease_config_file", g_disease_config_file_adapted);
		pt_config.put("run.r0", g_transmission_rate_measles);
	}
	if (test_tag == "maximum") {
		pt_config.put("run.r0", g_transmission_rate_maximum);
	}

	// -----------------------------------------------------------------------------------------
	// Initialize the logger.
	// -----------------------------------------------------------------------------------------
	spdlog::set_async_mode(1048576);
	auto file_logger = spdlog::rotating_logger_mt(
	    "contact_logger", g_output_prefix + "_logfile", std::numeric_limits<size_t>::max(),
	    std::numeric_limits<size_t>::max());
	file_logger->set_pattern("%v"); // Remove meta data from log => time-stamp of logging

	// -----------------------------------------------------------------------------------------
	// Initialize the simulation.
	// -----------------------------------------------------------------------------------------
	cout << "Building the simulator. " << endl;
	auto sim = SimulatorBuilder::Build(pt_config, file_logger, num_threads, track_index_case);
	cout << "Done building the simulator. " << endl << endl;

	// -----------------------------------------------------------------------------------------
	// Run the simulation.
	// -----------------------------------------------------------------------------------------
	const unsigned int num_days = pt_config.get<unsigned int>("run.num_days");
	for (unsigned int i = 0; i < num_days; i++) {
		sim->TimeStep(SimulationStepInput());
	}

	// -----------------------------------------------------------------------------------------
	// Release and close logger.
	// -----------------------------------------------------------------------------------------
	spdlog::drop_all();

	// -----------------------------------------------------------------------------------------
	// Round up.
	// -----------------------------------------------------------------------------------------
	const unsigned int num_cases = sim->GetPopulation()->get_infected_count();
	ASSERT_NEAR(num_cases, g_results.at(test_tag), 15000) << "!! CHANGED !!";

	stride::util::parallel::try_set_number_of_threads(old_number_of_threads);
}

namespace {
string scenarios[]{"default", "seeding_rate", "immunity_rate", "measles", "maximum"};

std::vector<unsigned int> threads = stride::util::parallel::using_parallelization_library
					? std::vector<unsigned int>({1U, 4U, 8U})
					: std::vector<unsigned int>({1U});
}

INSTANTIATE_TEST_CASE_P(
    Run_default, BatchDemos, ::testing::Combine(::testing::Values(string("default")), ::testing::ValuesIn(threads)));

INSTANTIATE_TEST_CASE_P(
    Run_seeding_rate, BatchDemos,
    ::testing::Combine(::testing::Values(string("seeding_rate")), ::testing::ValuesIn(threads)));

INSTANTIATE_TEST_CASE_P(
    Run_immunity_rate, BatchDemos,
    ::testing::Combine(::testing::Values(string("immunity_rate")), ::testing::ValuesIn(threads)));

INSTANTIATE_TEST_CASE_P(
    Run_measles, BatchDemos, ::testing::Combine(::testing::Values(string("measles")), ::testing::ValuesIn(threads)));

INSTANTIATE_TEST_CASE_P(
    Run_maximum, BatchDemos, ::testing::Combine(::testing::Values(string("maximum")), ::testing::ValuesIn(threads)));

} // end-of-namespace-Tests
