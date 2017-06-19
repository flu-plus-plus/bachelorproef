#include "Simulator.h"

#include "alias/Alias.h"
#include "calendar/Calendar.h"
#include "calendar/DaysOffStandard.h"
#include "core/Cluster.h"
#include "core/ClusterType.h"
#include "core/Infector.h"
#include "core/LogMode.h"
#include "multiregion/Visitor.h"
#include "pop/Population.h"
#include "util/Parallel.h"

#include <memory>
#include <boost/property_tree/ptree.hpp>
#include <spdlog/spdlog.h>

namespace stride {

using namespace std;
using namespace boost::property_tree;
using namespace stride::util;

Simulator::Simulator()
    : m_config(), m_num_threads(1U), m_log_level(LogMode::Null), m_population(nullptr), m_disease_profile(),
      m_track_index_case(false)
{
}

void Simulator::SetTrackIndexCase(bool track_index_case) { m_track_index_case = track_index_case; }

template <LogMode log_level, bool track_index_case, typename local_information_policy>
void Simulator::UpdateClusters()
{
	auto log = m_log;

	auto action = [this, log](Cluster& cluster, unsigned int thread_id) {
		Infector<log_level, track_index_case, local_information_policy>::Execute(
		    cluster, m_disease_profile, m_rng_handler[thread_id], m_calendar, log);
	};

	stride::util::parallel::parallel_for(m_clusters.m_households, m_num_threads, action);
	stride::util::parallel::parallel_for(m_clusters.m_school_clusters, m_num_threads, action);
	stride::util::parallel::parallel_for(m_clusters.m_work_clusters, m_num_threads, action);
	stride::util::parallel::parallel_for(m_clusters.m_primary_community, m_num_threads, action);
	stride::util::parallel::parallel_for(m_clusters.m_secondary_community, m_num_threads, action);
}

void Simulator::AddPersonToClusters(const Person& person)
{
	// Cluster id '0' means "not present in any cluster of that type".
	auto hh_id = person.GetClusterId(ClusterType::Household);
	if (hh_id > 0) {
		m_clusters.m_households[hh_id].AddPerson(person);
	}
	auto sc_id = person.GetClusterId(ClusterType::School);
	if (sc_id > 0) {
		m_clusters.m_school_clusters[sc_id].AddPerson(person);
	}
	auto wo_id = person.GetClusterId(ClusterType::Work);
	if (wo_id > 0) {
		m_clusters.m_work_clusters[wo_id].AddPerson(person);
	}
	auto primCom_id = person.GetClusterId(ClusterType::PrimaryCommunity);
	if (primCom_id > 0) {
		m_clusters.m_primary_community[primCom_id].AddPerson(person);
	}
	auto secCom_id = person.GetClusterId(ClusterType::SecondaryCommunity);
	if (secCom_id > 0) {
		m_clusters.m_secondary_community[secCom_id].AddPerson(person);
	}
}

void Simulator::RemovePersonFromClusters(const Person& person)
{
	// Cluster id '0' means "not present in any cluster of that type".
	auto hh_id = person.GetClusterId(ClusterType::Household);
	if (hh_id > 0) {
		m_clusters.m_households[hh_id].RemovePerson(person);
	}
	auto sc_id = person.GetClusterId(ClusterType::School);
	if (sc_id > 0) {
		m_clusters.m_school_clusters[sc_id].RemovePerson(person);
	}
	auto wo_id = person.GetClusterId(ClusterType::Work);
	if (wo_id > 0) {
		m_clusters.m_work_clusters[wo_id].RemovePerson(person);
	}
	auto primCom_id = person.GetClusterId(ClusterType::PrimaryCommunity);
	if (primCom_id > 0) {
		m_clusters.m_primary_community[primCom_id].RemovePerson(person);
	}
	auto secCom_id = person.GetClusterId(ClusterType::SecondaryCommunity);
	if (secCom_id > 0) {
		m_clusters.m_secondary_community[secCom_id].RemovePerson(person);
	}
}

PersonId Simulator::GeneratePersonId()
{
	if (m_unused_person_ids.empty()) {
		return m_population->get_max_id() + 1;
	} else {
		auto result = m_unused_person_ids.front();
		m_unused_person_ids.pop();
		return result;
	}
}

std::size_t Simulator::GenerateHousehold()
{
	std::size_t household_id;
	if (m_unused_households.empty()) {
		household_id = m_clusters.m_households.size();
		m_clusters.m_households.emplace_back(household_id, ClusterType::Household);
	} else {
		household_id = m_unused_households.front();
		m_unused_households.pop();
	}
	return household_id;
}

void Simulator::RecyclePersonId(PersonId id) { m_unused_person_ids.push(id); }

void Simulator::RecycleHousehold(std::size_t household_id) { m_unused_households.push(household_id); }

void Simulator::AcceptVisitors(const multiregion::SimulationStepInput& input)
{
	for (const auto& returning_expat : input.expatriates) {
		// Return the expatriate to this region's population.
		const auto& home_expat =
		    *m_population->emplace(m_expatriates.ExtractExpatriate(returning_expat.GetId()));

		// Update the expatriate's stats.
		home_expat.GetHealth() = returning_expat.GetHealth();
		if (returning_expat.IsParticipatingInSurvey()) {
			home_expat.ParticipateInSurvey();
		}

		// Add the returning expatriate to their clusters.
		AddPersonToClusters(home_expat);
	}

	for (const auto& visitor : input.visitors) {
		// Generate local ids and create a household cluster for the visitor.
		auto id = GeneratePersonId();
		auto household_id = GenerateHousehold();
		auto work_id = (*m_travel_rng)(m_clusters.m_work_clusters.size() - 1);
		auto primary_community_id = (*m_travel_rng)(m_clusters.m_primary_community.size() - 1);
		auto secondary_community_id = (*m_travel_rng)(m_clusters.m_secondary_community.size() - 1);

		// Insert the visitor in the population.
		Person local_visitor = *m_population->emplace(
		    id, visitor.person.GetAge(), household_id, 0, work_id, primary_community_id, secondary_community_id,
		    disease::Fate());

		// Set the visitor's health.
		local_visitor.GetHealth() = visitor.person.GetHealth();

		// Add the visitor to their assigned clusters.
		AddPersonToClusters(local_visitor);

		// Add an entry to the visitor log.
		multiregion::VisitorId visitor_desc;
		visitor_desc.home_id = visitor.person.GetId();
		visitor_desc.visitor_id = id;
		m_visitors.AddVisitor(visitor_desc, visitor.home_region, visitor.return_day);
	}
}

multiregion::SimulationStepOutput Simulator::ReturnVisitors()
{
	// First, find visitors which we can return.
	std::vector<multiregion::OutgoingVisitor> returning_expatriates;
	auto today = m_calendar->GetSimulationDay();
	for (const auto& expatriate_pair : m_visitors.ExtractVisitors(today)) {
		for (const auto& expatriate : expatriate_pair.second) {
			auto person = m_population->extract(expatriate.visitor_id);

			// Recycle the person's id and their household.
			RecyclePersonId(person.GetId());
			RecycleHousehold(person.GetClusterId(ClusterType::Household));

			// Restore the person's id to their home id.
			returning_expatriates.emplace_back(
			    person.WithId(expatriate.home_id), expatriate_pair.first, today);
		}
	}

	// Next, create a list of people which we'd like to send elsewhere.
	std::vector<multiregion::OutgoingVisitor> outgoing_visitors;
	auto travel_model = m_config.travel_model;

	// Build a model of where we want to send people.
	std::unordered_map<multiregion::RegionId, double> probabilities;
	for (const auto& airport : travel_model->GetLocalAirports()) {
		double route_probability_sum = 0.0;
		for (const auto& route : airport->routes) {
			route_probability_sum += route.passenger_fraction;
		}
		for (const auto& route : airport->routes) {
			probabilities[route.target->region_id] +=
			    route.passenger_fraction * airport->passenger_fraction / route_probability_sum;
		}
	}

	if (probabilities.size() == 0) {
		// Looks like we won't be sending people anywhere anytime soon.
		return {std::move(outgoing_visitors), std::move(returning_expatriates)};
	}

	auto target_region_generator =
	    alias::BiasedRandomValueGenerator<multiregion::RegionId>::CreateDistribution(probabilities, *m_travel_rng);

	// Gather the people we're going to ship off to another region.
	auto number_of_visitors = static_cast<std::size_t>(
	    std::max(
		0.0, std::floor(
			 static_cast<double>(m_population->size() - m_visitors.GetVisitorCount()) *
			 travel_model->GetTravelFraction())));

	for (auto visitor :
	     m_population->get_random_persons(*m_travel_rng, number_of_visitors, [this](const Person& p) -> bool {
		     return !m_visitors.IsVisitor(p.GetId());
	     })) {
		// Pick a region to which we'll send this person.
		auto target_region_id = target_region_generator.Next();

		// Remove the person from their clusters.
		RemovePersonFromClusters(visitor);

		auto return_date =
		    today + (*m_travel_rng)(
				(int)travel_model->GetMinTravelDuration(), (int)travel_model->GetMaxTravelDuration());

		outgoing_visitors.emplace_back(visitor, target_region_id, return_date);

		// Remove the person from the population and add them to the expatriate journal.
		m_expatriates.AddExpatriate(m_population->extract(visitor.GetId()));
	}

	return {std::move(outgoing_visitors), std::move(returning_expatriates)};
}

multiregion::SimulationStepOutput Simulator::TimeStep(const multiregion::SimulationStepInput& input)
{
	AcceptVisitors(input);
	shared_ptr<DaysOffInterface> days_off{nullptr};

	// Logic where you compute (on the basis of input/config for initial day
	// or on the basis of number of sick persons, duration of epidemic etc)
	// what kind of DaysOff scheme you apply. If we want to make this cluster
	// dependent then the days_off object has to be passed into the Update function.
	days_off = make_shared<DaysOffStandard>(m_calendar);
	const bool is_work_off{days_off->IsWorkOff()};
	const bool is_school_off{days_off->IsSchoolOff()};

	const double fraction_infected = m_population->get_fraction_infected();

	m_population->parallel_for(m_num_threads, [=](const Person& p, unsigned int) {
		p.Update(is_work_off, is_school_off, fraction_infected);
	});

	if (m_track_index_case) {
		switch (m_log_level) {
		case LogMode::Contacts:
			UpdateClusters<LogMode::Contacts, true>();
			break;
		case LogMode::Transmissions:
			UpdateClusters<LogMode::Transmissions, true>();
			break;
		case LogMode::None:
			UpdateClusters<LogMode::None, true>();
			break;
		default:
			throw runtime_error(std::string(__func__) + "Log mode screwed up!");
		}
	} else {
		switch (m_log_level) {
		case LogMode::Contacts:
			UpdateClusters<LogMode::Contacts, false>();
			break;
		case LogMode::Transmissions:
			UpdateClusters<LogMode::Transmissions, false>();
			break;
		case LogMode::None:
			UpdateClusters<LogMode::None, false>();
			break;
		default:
			throw runtime_error(std::string(__func__) + "Log mode screwed up!");
		}
	}

	m_calendar->AdvanceDay();
	return ReturnVisitors();
}

#if USE_HDF5

	/// Writes the current checkpoint
	void Simulator::SaveCheckPoint(unsigned int day)
	{
		cp->OpenFile();
		cp->SaveCheckPoint(*this, day);
		cp->CloseFile();
	}

	/// Writes the Atlas
	void Simulator::WriteAtlas()
	{
		cp->OpenFile();
		cp->WriteAtlas(m_population->get_atlas());
		cp->CloseFile();
	}

	/// Creates the checkpoint
	void Simulator::CreateCheckPoint(const std::string& name, const std::string& calendarName)
	{
		cp = std::make_unique<checkpoint::CheckPoint>(name);
		cp->CreateFile();
		cp->OpenFile();
		cp->WriteConfig(m_config);
		cp->WriteHolidays(calendarName);
		cp->CloseFile();
	}
#endif

} // end_of_namespace
