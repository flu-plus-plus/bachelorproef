#ifndef SIMULATOR_BUILDER_H_INCLUDED
#define SIMULATOR_BUILDER_H_INCLUDED

#include "Simulator.h"
#include "sim/SimulationConfig.h"

#include <memory>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <spdlog/spdlog.h>

#if USE_HDF5
#include "checkpoint/CheckPoint.h"
#endif

namespace stride {

class Population;
class Calendar;

/**
 * Main class that contains and direct the virtual world.
 */
class SimulatorBuilder
{
public:
	/// Build simulator.
	static std::shared_ptr<Simulator> Build(
	    const std::string& config_file_name, const std::shared_ptr<spdlog::logger>& log,
	    unsigned int num_threads = 1U, bool track_index_case = false);

	/// Build simulator.
	static std::shared_ptr<Simulator> Build(
	    const boost::property_tree::ptree& pt_config, const std::shared_ptr<spdlog::logger>& log,
	    unsigned int num_threads = 1U, bool track_index_case = false);

	/// Build simulator.
	static std::shared_ptr<Simulator> Build(
	    const SingleSimulationConfig& config, const std::shared_ptr<spdlog::logger>& log,
	    unsigned int num_threads = 1U);

	/// Build simulator.
	static std::shared_ptr<Simulator> Build(
	    const boost::property_tree::ptree& pt_config, const boost::property_tree::ptree& pt_disease,
	    const boost::property_tree::ptree& pt_contact, const std::shared_ptr<spdlog::logger>& log,
	    unsigned int number_of_threads = 1U, bool track_index_case = false);

	/// Build simulator.
	static std::shared_ptr<Simulator> Build(
	    const SingleSimulationConfig& config, const boost::property_tree::ptree& pt_disease,
	    const boost::property_tree::ptree& pt_contact, const std::shared_ptr<spdlog::logger>& log,
	    unsigned int number_of_threads = 1U);

#if USE_HDF5
	/// Load simulator.
	static std::shared_ptr<Simulator> Load(
	    const SingleSimulationConfig& config, const std::shared_ptr<spdlog::logger>& log,
	    const std::unique_ptr<checkpoint::CheckPoint>&, const boost::gregorian::date&,
	    unsigned int num_threads = 1U);
#endif

private:
	/// Initialize the clusters.
	static void InitializeClusters(std::shared_ptr<Simulator> sim);
};

} // end_of_namespace

#endif // end-of-include-guard
