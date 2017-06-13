#ifndef MULTIREGION_PARALLEL_SIMULATION_MANAGER_H_INCLUDED
#define MULTIREGION_PARALLEL_SIMULATION_MANAGER_H_INCLUDED

/**
 * @file
 * Parallel multi-region data structures for the simulator.
 */

#include <memory>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>
#include "multiregion/LocalSimulationTask.h"
#include "multiregion/SequentialSimulationManager.h"
#include "multiregion/SimulationManager.h"
#include "sim/Simulator.h"
#include "sim/SimulatorBuilder.h"

namespace stride {
namespace multiregion {

/**
 * A parallel simulation manager implementation.
 */
template <typename TResult, typename... TInitialResultArgs>
class ParallelSimulationManager final : public SimulationManager<TResult, TInitialResultArgs...>
{
private:
	class ParallelTaskCommunicator final
	{
	public:
		ParallelTaskCommunicator(
		    RegionId id, ParallelSimulationManager<TResult, TInitialResultArgs...>* manager)
		    : id(id), manager(manager)
		{
		}

		SimulationStepInput Pull()
		{
			std::lock_guard<std::mutex> lock(manager->comm_mutex);
			return manager->comm_data.Pull(id);
		}

		void Push(const SimulationStepOutput& data)
		{
			std::lock_guard<std::mutex> lock(manager->comm_mutex);
			manager->comm_data.Push(id, manager->tasks[id]->GetConnectedRegions(), data);
		}

	private:
		RegionId id;
		ParallelSimulationManager<TResult, TInitialResultArgs...>* manager;
	};

	bool TryPopReady(RegionId& id)
	{
		auto success = comm_data.TryPopReady(id);
		if (success) {
			comm_data.ResetDependencies(id, tasks[id]->GetConnectedRegions());
		}
		return success;
	}

	TaskCommunicationData comm_data;
	std::unordered_map<RegionId, std::shared_ptr<LocalSimulationTask<TResult, ParallelTaskCommunicator>>> tasks;
	std::mutex comm_mutex;
	std::size_t number_of_task_threads;
	unsigned int number_of_sim_threads;
	volatile std::size_t active_task_count;

public:
	ParallelSimulationManager(std::size_t number_of_task_threads, unsigned int number_of_sim_threads)
	    : number_of_task_threads(number_of_task_threads), number_of_sim_threads(number_of_sim_threads),
	      active_task_count(0)
	{
	}

	/// Creates and initiates a new simulation task based on the given configuration.
	std::shared_ptr<SimulationTask<TResult>> CreateSimulation(
	    const SingleSimulationConfig& configuration, const std::shared_ptr<spdlog::logger>& log,
	    TInitialResultArgs... args) final override
	{
		// Build a simulator.
		auto sim = SimulatorBuilder::Build(configuration, log, number_of_sim_threads);
		auto id = configuration.travel_model->GetRegionId();
		auto task = std::make_shared<LocalSimulationTask<TResult, ParallelTaskCommunicator>>(
		    sim, ParallelTaskCommunicator(id, this), args..., configuration.common_config->generate_vis_file);
		tasks[id] = task;
		comm_data.MarkReady(id);
		active_task_count++;
		return task;
	}

#if USE_HDF5
	/// Loads a new simulation task from the Checkpoint.
	std::shared_ptr<SimulationTask<TResult>> LoadSimulation(
	    const SingleSimulationConfig& configuration, const std::shared_ptr<spdlog::logger>& log,
	    const std::unique_ptr<checkpoint::CheckPoint>& cp, const boost::gregorian::date& date,
	    TInitialResultArgs... args) final override
	{
		// Build a simulator.
		auto sim = SimulatorBuilder::Load(configuration, log, cp, date, number_of_sim_threads);
		auto id = configuration.travel_model->GetRegionId();
		auto task = std::make_shared<LocalSimulationTask<TResult, ParallelTaskCommunicator>>(
		    sim, ParallelTaskCommunicator(id, this), args..., configuration.common_config->generate_vis_file);
		tasks[id] = task;
		comm_data.MarkReady(id);
		active_task_count++;
		return task;
	}
#endif

	/// Waits for all tasks to complete.
	void WaitAll() final override
	{
		// Start 'number_of_task_threads' threads.
		std::vector<std::thread> threads;
		for (std::size_t i = 0; i < number_of_task_threads; i++) {
			threads.emplace_back([this]() {
				// We will leave at least one thread running until the number of active tasks reaches
				// zero.
				while (active_task_count > 0) {
					// Acquire a lock, so we don't get weird race conditions like popping the same
					// task twice.
					comm_mutex.lock();

					// Try to pop a task that's ready to perform a time step.
					RegionId ready_id;
					if (TryPopReady(ready_id)) {
						auto task = tasks[ready_id];
						if (task->IsDone()) {
							// This task's done. We ought to decrement the active task
							// counter and we can also consider ending this thread.
							auto count = --active_task_count;
							comm_mutex.unlock();
							if (count < number_of_task_threads) {
								// End this thread of execution. We don't need it
								// anymore.
								break;
							}
						} else {
							// Have the task perform a single step. Release the
							// communication lock early so other threads can proceed.
							comm_mutex.unlock();
							task->Step();
						}
					} else {
						// No task was ready. Free the lock to give the other tasks the
						// opportunity to push their outputs and try again.
						comm_mutex.unlock();
					}
				}
			});
		}
		// Wait for all the threads to complete.
		for (auto& t : threads) {
			t.join();
		}
	}
};

} // namespace
} // namespace

#endif // end-of-include-guard
