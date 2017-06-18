#ifndef MULTIREGION_SIMULATION_MANAGER_H_INCLUDED
#define MULTIREGION_SIMULATION_MANAGER_H_INCLUDED

/**
 * @file
 * Multi-region data structures for the simulator.
 */

#include <functional>
#include <memory>
#include <vector>
#include <boost/any.hpp>
#include <boost/property_tree/ptree.hpp>
#include "pop/Population.h"
#include "sim/SimulationConfig.h"

namespace stride {
namespace multiregion {

/**
 * An abstract class that can be used to communicate with and control a single simulation.
 */
template <typename TResult>
class SimulationTask
{
public:
	/// Fetches this simulation task's result.
	virtual TResult GetResult() = 0;

	/// Applies the given aggregation function to this simulation task's population.
	template <typename TAggregate>
	TAggregate Aggregate(TAggregate apply(const PopulationRef&))
	{
		return boost::any_cast<TAggregate>(
		    AggregateAny([apply](const PopulationRef& pop) -> boost::any { return boost::any(apply(pop)); }));
	}

	/// Gets the simulation task's population.
	size_t GetPopulationSize()
	{
		return Aggregate<size_t>([](const PopulationRef& pop) -> std::size_t { return pop->size(); });
	}

	/// Gets the number of people that are infected in the simulation task's population.
	size_t GetInfectedCount()
	{
		return Aggregate<size_t>(
		    [](const PopulationRef& pop) -> std::size_t { return pop->get_infected_count(); });
	}

	/// Gets this simulation task's population.
	PopulationRef GetPopulation()
	{
		return Aggregate<PopulationRef>([](const PopulationRef& pop) -> PopulationRef { return pop; });
	}

	/// Applies the given aggregation function to this simulation task's population.
	virtual boost::any AggregateAny(std::function<boost::any(const PopulationRef&)>) = 0;
};

/**
 * An abstract class that initiates simulations.
 */
template <typename TResult, typename... TInitialResultArgs>
struct SimulationManager
{
	/// Creates a new simulation task based on the given configuration.
	virtual std::shared_ptr<SimulationTask<TResult>> CreateSimulation(
	    const SingleSimulationConfig& configuration, const std::shared_ptr<spdlog::logger>& log,
	    TInitialResultArgs... args) = 0;
#if USE_HDF5
	/// Loads a simulation from the checkpoint
	virtual std::shared_ptr<SimulationTask<TResult>> LoadSimulation(
	    const SingleSimulationConfig& configuration, const std::shared_ptr<spdlog::logger>& log,
	    const std::string& cp, const boost::gregorian::date& date, TInitialResultArgs... args) = 0;
#endif

	/// Waits for all simulation tasks to complete.
	virtual void WaitAll() = 0;
};

} // namespace
} // namespace

#endif // end-of-include-guard
