#ifndef MULTIREGION_SEQUENTIAL_SIMULATION_MANAGER_H_INCLUDED
#define MULTIREGION_SEQUENTIAL_SIMULATION_MANAGER_H_INCLUDED

/**
 * @file
 * Sequential multi-region data structures for the simulator.
 */

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include "multiregion/LocalSimulationTask.h"
#include "multiregion/SimulationManager.h"
#include "multiregion/Visitor.h"
#include "sim/Simulator.h"
#include "sim/SimulatorBuilder.h"

namespace stride {
namespace multiregion {

/// Defines a communication buffer for a single task.
class TaskCommunicationBuffer final
{
public:
	/// Tells if this communication buffer is ready for a pull operation by checking if all of its dependencies have
	/// been satisfied.
	bool IsReady() const { return unsatisfied_dependencies.size() == 0; }

	/// Gets the communication buffer's phase, i.e., the simulation day that will be pulled next.
	std::size_t GetPhase() const { return phase; }

	/// Satisfied the given dependency.
	void SatisfyDependency(RegionId dependency) { unsatisfied_dependencies.erase(dependency); }

	/// Pulls the data from this buffer.
	SimulationStepInput Pull()
	{
		SimulationStepInput result = std::move(pull_buffers[phase]);
		pull_buffers.erase(phase);
		phase++;
		return result;
	}

	/// Pushes a visitor from the given region into this buffer.
	void PushVisitor(std::size_t source_region_phase, RegionId source_region_id, const OutgoingVisitor& visitor)
	{
		pull_buffers[source_region_phase].visitors.emplace_back(
		    visitor.person, source_region_id, visitor.return_day);
	}

	/// Pushes an expatriate from the given region into this buffer.
	void PushExpatriate(std::size_t source_region_phase, const Person& expatriate)
	{
		pull_buffers[source_region_phase].expatriates.emplace_back(expatriate);
	}

	/// Sets this buffer's dependencies to the given set of dependencies.
	void ResetDependencies(const std::unordered_set<RegionId>& new_unsatisfied_dependencies)
	{
		unsatisfied_dependencies = new_unsatisfied_dependencies;
	}

private:
	/// The task's next pull result, which is mutable.
	std::unordered_map<std::size_t, SimulationStepInput> pull_buffers;

	/// The task's current phase, i.e., the simulation day that will be pulled next.
	std::size_t phase;

	/// The set of all task dependencies that have not been satisfied yet.
	std::unordered_set<RegionId> unsatisfied_dependencies;
};

/// Contains common data for a graph of communicating tasks.
class TaskCommunicationData final
{
public:
	/// Tries to find a task that's ready.
	bool TryPopReady(RegionId& id)
	{
		if (ready_tasks.empty()) {
			return false;
		}

		id = *ready_tasks.begin();
		ready_tasks.erase(id);
		return true;
	}

	/// Marks the task with the given id as ready.
	void MarkReady(RegionId id) { ready_tasks.insert(id); }

	/// Pulls input data for the task with the given id.
	SimulationStepInput Pull(RegionId id) { return buffers[id].Pull(); }

	/// Pushes output data for the task with the given id and dependencies.
	void Push(RegionId id, const std::unordered_set<RegionId>& dependencies, const SimulationStepOutput& data)
	{
		auto phase = buffers[id].GetPhase();
		for (const auto& outgoing_visitor : data.visitors) {
			buffers[outgoing_visitor.visited_region].PushVisitor(phase, id, outgoing_visitor);
		}
		for (const auto& returning_expatriate : data.expatriates) {
			buffers[returning_expatriate.visited_region].PushExpatriate(phase, returning_expatriate.person);
		}
		for (const auto& dep : dependencies) {
			auto& buf = buffers[dep];
			buf.SatisfyDependency(id);
			if (buf.IsReady()) {
				MarkReady(dep);
			}
		}
		if (buffers[id].IsReady()) {
			MarkReady(id);
		}
	}

	/// Resets the dependencies of the region with the given id.
	void ResetDependencies(RegionId id, const std::unordered_set<RegionId>& dependencies)
	{
		buffers[id].ResetDependencies(dependencies);
	}

private:
	std::unordered_set<RegionId> ready_tasks;
	std::unordered_map<RegionId, TaskCommunicationBuffer> buffers;
};

/**
 * A sequential simulation manager implementation.
 */
template <typename TResult, typename... TInitialResultArgs>
class SequentialSimulationManager final : public SimulationManager<TResult, TInitialResultArgs...>
{
private:
	class SequentialTaskCommunicator final
	{
	public:
		SequentialTaskCommunicator(
		    RegionId id, SequentialSimulationManager<TResult, TInitialResultArgs...>* manager)
		    : id(id), manager(manager)
		{
		}

		SimulationStepInput Pull() { return manager->comm_data.Pull(id); }

		void Push(const SimulationStepOutput& data)
		{
			manager->comm_data.Push(id, manager->tasks[id]->GetConnectedRegions(), data);
		}

	private:
		RegionId id;
		SequentialSimulationManager<TResult, TInitialResultArgs...>* manager;
	};

	TaskCommunicationData comm_data;
	std::unordered_map<RegionId, std::shared_ptr<LocalSimulationTask<TResult, SequentialTaskCommunicator>>> tasks;
	unsigned int number_of_sim_threads;

public:
	SequentialSimulationManager(unsigned int number_of_sim_threads) : number_of_sim_threads(number_of_sim_threads)
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
		auto task = std::make_shared<LocalSimulationTask<TResult, SequentialTaskCommunicator>>(
		    sim, SequentialTaskCommunicator(id, this), args..., configuration.common_config->generate_vis_file);
		tasks[id] = task;
		comm_data.MarkReady(id);
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
		auto task = std::make_shared<LocalSimulationTask<TResult, SequentialTaskCommunicator>>(
		    sim, SequentialTaskCommunicator(id, this), args..., configuration.common_config->generate_vis_file);
		tasks[id] = task;
		comm_data.MarkReady(id);
		return task;
	}
#endif

	/// Waits for all tasks to complete.
	void WaitAll() final override
	{
		RegionId ready_id;
		while (comm_data.TryPopReady(ready_id)) {
			comm_data.ResetDependencies(ready_id, tasks[ready_id]->GetConnectedRegions());
			auto task = tasks[ready_id];
			if (!task->IsDone()) {
				task->Step();
			}
		}
	}
};

} // namespace
} // namespace

#endif // end-of-include-guard
