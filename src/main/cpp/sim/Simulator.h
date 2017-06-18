#ifndef SIMULATOR_H_INCLUDED
#define SIMULATOR_H_INCLUDED

#include "behaviour/information_policies/NoLocalInformation.h"
#include "core/Cluster.h"
#include "core/DiseaseProfile.h"
#include "core/LogMode.h"
#include "core/RngHandler.h"
#include "multiregion/Visitor.h"
#include "multiregion/VisitorJournal.h"
#include "pop/Population.h"
#include "sim/SimulationConfig.h"

#include <memory>
#include <queue>
#include <vector>
#include <boost/property_tree/ptree.hpp>
#include <spdlog/spdlog.h>

#if USE_HDF5
#include "checkpoint/CheckPoint.h"
#endif

namespace stride {

#if USE_HDF5
namespace checkpoint {
class CheckPoint;
}
#endif

class Calendar;

struct ClusterStruct
{
	/// Container with household Clusters.
	std::vector<Cluster> m_households;

	/// Container with school Clusters.
	std::vector<Cluster> m_school_clusters;

	/// Container with work Clusters.
	std::vector<Cluster> m_work_clusters;

	/// Container with primary community Clusters.
	std::vector<Cluster> m_primary_community;

	/// Container with secondary community Clusters.
	std::vector<Cluster> m_secondary_community;
};

/**
 * Main class that contains and direct the virtual world.
 */
class Simulator
{
public:
	// Default constructor for empty Simulator.
	Simulator();

	/// Get the population.
	PopulationRef GetPopulation() const { return m_population; }

	/// Gets the simulator's configuration.
	SingleSimulationConfig GetConfiguration() const { return m_config; }

	/// Gets the simulator's date.
	boost::gregorian::date GetDate() const { return m_calendar->GetDate(); }

	/// Gets the clusters in this simulation. This is for saving.
	const ClusterStruct& GetClusters() const { return m_clusters; }

	/// Gets the clusters in this simulation. This is for loading.
	ClusterStruct& GetClusters() { return m_clusters; }

	/// Sets the population.
	void SetPopulation(const std::shared_ptr<Population>& population) { m_population = population; }

	/// Sets the visitor journal
	void SetVisitors(const multiregion::VisitorJournal& visitors) { m_visitors = visitors; }

	/// Sets the expatriate journal
	void SetExpatriates(const multiregion::ExpatriateJournal& expatriates) { m_expatriates = expatriates; }

	/// Change track_index_case setting.
	void SetTrackIndexCase(bool track_index_case);

	/// Run one time step, computing full simulation (default) or only index case.
	multiregion::SimulationStepOutput TimeStep(const multiregion::SimulationStepInput& input);

	/// Tests if this simulation has run to completion.
	bool IsDone() const { return m_calendar->GetSimulationDay() >= m_config.common_config->number_of_days; }

	/// Tests if the person is a visitor to this simulation.
	bool IsVisitor(PersonId id) const { return m_visitors.IsVisitor(id); }

	/// Gets the visitor journal
	multiregion::VisitorJournal GetVistiorJournal() const { return m_visitors; }

	/// Gets the expatriate journal
	multiregion::ExpatriateJournal GetExpatriateJournal() const { return m_expatriates; }

	/// Runs the given action on every resident who is currently present
	/// in the simulation. More than one invocation of `action` may be
	/// running simultaneously. `action` must be invocable and have
	/// signature `void(const Person& person, unsigned int thread_number)`.
	template <typename TAction>
	void ParallelForeachPresentResident(const TAction& action)
	{
		m_population->parallel_for(m_num_threads, [this, &action](const Person& p, unsigned int thread_number) {
			if (!IsVisitor(p.GetId())) {
				action(p, thread_number);
			}
		});
	}

	/// Runs the given action on every resident of the simulation, present
	/// or otherwise. More than one invocation of `action` may be
	/// running simultaneously. `action` must be invocable and have
	/// signature `void(const Person& person, unsigned int thread_number)`.
	template <typename TAction>
	void ParallelForeachResident(const TAction& action)
	{
		ParallelForeachPresentResident<TAction>(action);
		m_expatriates.SerialForeach(action);
	}

	/// Runs the given action on every person who is currently present
	/// in the simulation, person or otherwise. More than one invocation
	/// of the action may be running simultaneously. `action` must be
	/// invocable and have signature
	/// `void(const Person& person, unsigned int thread_number)`.
	template <typename TAction>
	void ParallelForeachPresentPerson(const TAction& action)
	{
		m_population->parallel_for(m_num_threads, action);
	}

	/// Runs the given action on every resident who is currently present
	/// in the simulation. No more than one invocation of `action` will be
	/// running simultaneously. `action` must be invocable and have
	/// signature `void(const Person& person, unsigned int dummy)`.
	template <typename TAction>
	void SerialForeachPresentResident(const TAction& action)
	{
		m_population->serial_for([this, &action](const Person& p, unsigned int thread_number) {
			if (!IsVisitor(p.GetId())) {
				action(p, thread_number);
			}
		});
	}

	/// Runs the given action on every resident of the simulation, present
	/// or otherwise. No more than one invocation of `action` will be running
	/// simultaneously. `action` must be invocable and have
	/// signature `void(const Person& person, unsigned int dummy)`.
	template <typename TAction>
	void SerialForeachResident(const TAction& action)
	{
		SerialForeachPresentResident<TAction>(action);
		m_expatriates.SerialForeach(action);
	}

	/// Runs the given action on every person who is currently present
	/// in the simulation, person or otherwise. No more than one invocation
	/// of `action` will be running simultaneously. `action` must be
	/// invocable and have signature
	/// `void(const Person& person, unsigned int dummy)`.
	template <typename TAction>
	void SerialForeachPresentPerson(const TAction& action)
	{
		m_population->serial_for(m_num_threads, action);
	}

#if USE_HDF5

	/// Writes the current checkpoint
	void SaveCheckPoint(unsigned int day);

	/// Writes the Atlas
	void WriteAtlas();

	/// Creates the checkpoint
	void CreateCheckPoint(const std::string& name, const std::string& calendarName);
#endif

private:
	/// Accepts visitors from other regions.
	void AcceptVisitors(const multiregion::SimulationStepInput& input);

	/// Returns visitors whose return trip is scheduled today and returns them to their
	/// home regions.
	multiregion::SimulationStepOutput ReturnVisitors();

	/// Adds the given person to the clusters they've been assigned to.
	void AddPersonToClusters(const Person& person);

	/// Removes the given person from the clusters they've been assigned to.
	void RemovePersonFromClusters(const Person& person);

	/// Generates an id for a person that is not in use.
	PersonId GeneratePersonId();

	/// Generates a new household cluster and returns its id.
	std::size_t GenerateHousehold();

	/// Recycles the given person id.
	void RecyclePersonId(PersonId id);

	/// Recycles the household with the given id.
	void RecycleHousehold(std::size_t household_id);

	/// Update the contacts in the given clusters.
	template <LogMode log_level, bool track_index_case = false,
		  typename local_information_policy = NoLocalInformation>
	void UpdateClusters();

private:
	/// Configuration for this simulator.
	SingleSimulationConfig m_config;

	/// Log for this simulator.
	std::shared_ptr<spdlog::logger> m_log;

private:
	/// The number of (OpenMP) threads.
	unsigned int m_num_threads;

	/// Pointer to the RngHandlers.
	std::vector<RngHandler> m_rng_handler;

	/// A random number generator for travel.
	std::shared_ptr<util::Random> m_travel_rng;

	/// Specifies logging mode.
	LogMode m_log_level;

	/// Management of calendar.
	std::shared_ptr<Calendar> m_calendar;

private:
	/// Pointer to the Population.
	std::shared_ptr<Population> m_population;

	/// Visitor journal.
	stride::multiregion::VisitorJournal m_visitors;

	/// Expatriate journal.
	stride::multiregion::ExpatriateJournal m_expatriates;

	/// Struct containing all Clusters.
	ClusterStruct m_clusters;

	/// A list of unused households which can are eligible for recycling.
	std::queue<std::size_t> m_unused_households;

	/// A list of unused person IDs which are eligible for recycling.
	std::queue<PersonId> m_unused_person_ids;

	/// Profile of disease.
	DiseaseProfile m_disease_profile;

	/// General simulation or tracking index case.
	bool m_track_index_case;

#if USE_HDF5
	/// The checkpoint connected to this simulator
	std::unique_ptr<checkpoint::CheckPoint> cp;
#endif

private:
	friend class SimulatorBuilder;
};

} // end_of_namespace

#endif // end-of-include-guard
