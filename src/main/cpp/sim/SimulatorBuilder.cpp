#include "SimulatorBuilder.h"

#include "calendar/Calendar.h"
#include "core/Cluster.h"
#include "core/ClusterType.h"
#include "core/ContactProfile.h"
#include "core/Infector.h"
#include "core/LogMode.h"
#include "pop/Population.h"
#include "pop/PopulationBuilder.h"
#include "sim/SimulationConfig.h"
#include "util/Errors.h"
#include "util/InstallDirs.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace stride {

using namespace std;
using namespace boost::filesystem;
using namespace boost::property_tree;
using namespace stride::util;

shared_ptr<Simulator> SimulatorBuilder::Build(
    const string& config_file_name, const std::shared_ptr<spdlog::logger>& log, unsigned int num_threads,
    bool track_index_case)
{
	// Configuration file.
	ptree pt_config;
	InstallDirs::ReadXmlFile(config_file_name, InstallDirs::GetCurrentDir(), pt_config);

	// Done.
	return Build(pt_config, log, num_threads, track_index_case);
}

shared_ptr<Simulator> SimulatorBuilder::Build(
    const ptree& pt_config, const std::shared_ptr<spdlog::logger>& log, unsigned int num_threads, bool track_index_case)
{
	SingleSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = track_index_case;
	return Build(config, log, num_threads);
}

shared_ptr<Simulator> SimulatorBuilder::Build(
    const SingleSimulationConfig& config, const std::shared_ptr<spdlog::logger>& log, unsigned int num_threads)
{
	// Disease file.
	ptree pt_disease;
	InstallDirs::ReadXmlFile(config.common_config->disease_config_file_name, InstallDirs::GetDataDir(), pt_disease);

	// Contact file.
	ptree pt_contact;
	InstallDirs::ReadXmlFile(config.common_config->contact_matrix_file_name, InstallDirs::GetDataDir(), pt_contact);

	// Done.
	return Build(config, pt_disease, pt_contact, log, num_threads);
}

shared_ptr<Simulator> SimulatorBuilder::Build(
    const ptree& pt_config, const ptree& pt_disease, const ptree& pt_contact,
    const std::shared_ptr<spdlog::logger>& log, unsigned int number_of_threads, bool track_index_case)
{
	SingleSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = track_index_case;
	return Build(config, pt_disease, pt_contact, log, number_of_threads);
}

shared_ptr<Simulator> SimulatorBuilder::Build(
    const SingleSimulationConfig& config, const ptree& pt_disease, const ptree& pt_contact,
    const std::shared_ptr<spdlog::logger>& log, unsigned int number_of_threads)
{
	auto sim = make_shared<Simulator>();

	// Initialize the simulator's configuration.
	sim->m_config = config;

	// Initialize the simulator's log.
	sim->m_log = log;

	// Initialize track_index_case policy
	sim->m_track_index_case = config.common_config->track_index_case;

	// Initialize number of threads.
	sim->m_num_threads = number_of_threads;

	// Initialize calendar.
	sim->m_calendar = make_shared<Calendar>(config.common_config->initial_calendar);

	// Get log level.
	sim->m_log_level = config.log_config->log_level;

	// Create a random number generator for the simulator.
	auto rng = std::make_shared<Random>(config.common_config->rng_seed);

	// Build population.
	sim->m_travel_rng = rng;
	sim->m_population = PopulationBuilder::Build(config, pt_disease, *rng, log);

	// Initialize clusters.
	InitializeClusters(sim);

	// Initialize disease profile.
	sim->m_disease_profile.Initialize(config, pt_disease);

	// Initialize Rng handlers
	unsigned int new_seed = (*rng)(numeric_limits<unsigned int>::max());
	for (size_t i = 0; i < sim->m_num_threads; i++) {
		sim->m_rng_handler.emplace_back(RngHandler(new_seed, sim->m_num_threads, i));
	}

	// Initialize contact profiles.
	Cluster::AddContactProfile(ClusterType::Household, ContactProfile(ClusterType::Household, pt_contact));
	Cluster::AddContactProfile(ClusterType::School, ContactProfile(ClusterType::School, pt_contact));
	Cluster::AddContactProfile(ClusterType::Work, ContactProfile(ClusterType::Work, pt_contact));
	Cluster::AddContactProfile(
	    ClusterType::PrimaryCommunity, ContactProfile(ClusterType::PrimaryCommunity, pt_contact));
	Cluster::AddContactProfile(
	    ClusterType::SecondaryCommunity, ContactProfile(ClusterType::SecondaryCommunity, pt_contact));

	// Done.
	return sim;
}

void SimulatorBuilder::InitializeClusters(shared_ptr<Simulator> sim)
{
	// Determine number of clusters.
	unsigned int max_id_households = 0U;
	unsigned int max_id_school_clusters = 0U;
	unsigned int max_id_work_clusters = 0U;
	unsigned int max_id_primary_community = 0U;
	unsigned int max_id_secondary_community = 0U;
	Population& population = *sim->m_population;

	population.serial_for([&](const Person& p, unsigned int) -> void {
		max_id_households = std::max(max_id_households, p.GetClusterId(ClusterType::Household));
		max_id_school_clusters = std::max(max_id_school_clusters, p.GetClusterId(ClusterType::School));
		max_id_work_clusters = std::max(max_id_work_clusters, p.GetClusterId(ClusterType::Work));
		max_id_primary_community =
		    std::max(max_id_primary_community, p.GetClusterId(ClusterType::PrimaryCommunity));
		max_id_secondary_community =
		    std::max(max_id_secondary_community, p.GetClusterId(ClusterType::SecondaryCommunity));
	});

	// Keep separate id counter to provide a unique id for every cluster.
	unsigned int cluster_id = 1;

	for (size_t i = 0; i <= max_id_households; i++) {
		sim->m_clusters.m_households.emplace_back(Cluster(cluster_id, ClusterType::Household));
		cluster_id++;
	}
	for (size_t i = 0; i <= max_id_school_clusters; i++) {
		sim->m_clusters.m_school_clusters.emplace_back(Cluster(cluster_id, ClusterType::School));
		cluster_id++;
	}
	for (size_t i = 0; i <= max_id_work_clusters; i++) {
		sim->m_clusters.m_work_clusters.emplace_back(Cluster(cluster_id, ClusterType::Work));
		cluster_id++;
	}
	for (size_t i = 0; i <= max_id_primary_community; i++) {
		sim->m_clusters.m_primary_community.emplace_back(Cluster(cluster_id, ClusterType::PrimaryCommunity));
		cluster_id++;
	}
	for (size_t i = 0; i <= max_id_secondary_community; i++) {
		sim->m_clusters.m_secondary_community.emplace_back(
		    Cluster(cluster_id, ClusterType::SecondaryCommunity));
		cluster_id++;
	}

	population.serial_for([&](const Person& p, unsigned int) -> void { sim->AddPersonToClusters(p); });
}

#if USE_HDF5
shared_ptr<Simulator> SimulatorBuilder::Load(
    const SingleSimulationConfig& config, const std::shared_ptr<spdlog::logger>& log,
    const std::unique_ptr<checkpoint::CheckPoint>& cp, const boost::gregorian::date& date, unsigned int num_threads)
{
	// Disease file.
	ptree pt_disease;
	InstallDirs::ReadXmlFile(config.common_config->disease_config_file_name, InstallDirs::GetDataDir(), pt_disease);

	// Contact file.
	ptree pt_contact;
	InstallDirs::ReadXmlFile(config.common_config->contact_matrix_file_name, InstallDirs::GetDataDir(), pt_contact);

	auto sim = make_shared<Simulator>();

	// Initialize the simulator's configuration.
	sim->m_config = config;

	// Initialize the simulator's log.
	sim->m_log = log;

	// Initialize track_index_case policy
	sim->m_track_index_case = config.common_config->track_index_case;

	// Initialize number of threads.
	sim->m_num_threads = num_threads;

	// Initialize calendar.
	sim->m_calendar = make_shared<Calendar>(config.common_config->initial_calendar);

	// Get log level.
	sim->m_log_level = config.log_config->log_level;

	// Create a random number generator for the simulator.
	auto rng = std::make_shared<Random>(config.common_config->rng_seed);

	// Build population.
	sim->m_travel_rng = rng;

	cp->OpenFile();
	cp->LoadCheckPoint(date, *sim);
	cp->CloseFile();

	// Initialize disease profile.
	sim->m_disease_profile.Initialize(config, pt_disease);

	// Initialize Rng handlers
	unsigned int new_seed = (*rng)(numeric_limits<unsigned int>::max());
	for (size_t i = 0; i < sim->m_num_threads; i++) {
		sim->m_rng_handler.emplace_back(RngHandler(new_seed, sim->m_num_threads, i));
	}

	// Initialize contact profiles.
	Cluster::AddContactProfile(ClusterType::Household, ContactProfile(ClusterType::Household, pt_contact));
	Cluster::AddContactProfile(ClusterType::School, ContactProfile(ClusterType::School, pt_contact));
	Cluster::AddContactProfile(ClusterType::Work, ContactProfile(ClusterType::Work, pt_contact));
	Cluster::AddContactProfile(
	    ClusterType::PrimaryCommunity, ContactProfile(ClusterType::PrimaryCommunity, pt_contact));
	Cluster::AddContactProfile(
	    ClusterType::SecondaryCommunity, ContactProfile(ClusterType::SecondaryCommunity, pt_contact));

	// Done.
	return sim;
}
#endif

} // end_of_namespace
