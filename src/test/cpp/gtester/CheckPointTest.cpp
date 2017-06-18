#include <iostream>
#include <vector>
#include <boost/filesystem.hpp>
#include <checkpoint/CheckPoint.h>
#include <core/ClusterType.h>
#include <gtest/gtest.h>
#include <pop/Population.h>
#include <sim/SimulatorBuilder.h>
#include <stdio.h>
#include <util/InstallDirs.h>

using namespace stride;

namespace Tests {

TEST(CheckPoint, CheckPoint) { stride::checkpoint::CheckPoint("test.h5"); }

TEST(CheckPoint, CreateFile)
{
	stride::checkpoint::CheckPoint cp("test.h5");
	cp.CreateFile();
	boost::filesystem::path f("test.h5");
	EXPECT_TRUE(boost::filesystem::is_regular_file(f));
	boost::filesystem::remove(f);
}

TEST(CheckPoint, Open_CloseFile)
{
	stride::checkpoint::CheckPoint cp("test.h5");
	cp.CreateFile();
	boost::filesystem::path f("test.h5");
	cp.OpenFile();
	cp.CloseFile();
	boost::filesystem::remove(f);
}

TEST(CheckPoint, Write_LoadConfig)
{
	boost::property_tree::ptree pt_config;
	util::InstallDirs::ReadXmlFile("config/run_default.xml", util::InstallDirs::GetRootDir(), pt_config);

	MultiSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = 0;

	auto toSave = config.AsSingleConfig();
	toSave.log_config->output_prefix = "testPrefix";

	stride::checkpoint::CheckPoint cp("WriteConfig.h5");
	cp.CreateFile();
	cp.OpenFile();
	cp.WriteConfig(toSave);
	cp.CloseFile();

	SingleSimulationConfig toCheck;

	cp.OpenFile();
	toCheck = cp.LoadSingleConfig();
	cp.CloseFile();

	EXPECT_EQ(toSave.common_config->track_index_case, toCheck.common_config->track_index_case);
	EXPECT_EQ(toSave.common_config->rng_seed, toCheck.common_config->rng_seed);
	EXPECT_EQ(toSave.common_config->r0, toCheck.common_config->r0);
	EXPECT_EQ(toSave.common_config->seeding_rate, toCheck.common_config->seeding_rate);
	EXPECT_EQ(toSave.common_config->immunity_rate, toCheck.common_config->immunity_rate);
	EXPECT_EQ(toSave.common_config->number_of_days, toCheck.common_config->number_of_days);
	EXPECT_EQ(
	    toSave.common_config->number_of_survey_participants, toCheck.common_config->number_of_survey_participants);

	EXPECT_EQ(toSave.log_config->output_prefix, toCheck.log_config->output_prefix);
	EXPECT_EQ(toSave.log_config->generate_person_file, toCheck.log_config->generate_person_file);
	EXPECT_EQ(toSave.log_config->log_level, toCheck.log_config->log_level);

	EXPECT_EQ(toSave.GetId(), toCheck.GetId());

	// TODO: check contents of files
	// TODO: check travelmodel
}

TEST(CheckPoint, Write_LoadHolidays)
{
	boost::property_tree::ptree pt_config;
	util::InstallDirs::ReadXmlFile("config/run_default.xml", util::InstallDirs::GetRootDir(), pt_config);

	MultiSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = 0;

	stride::checkpoint::CheckPoint cp("WriteHolidays.h5");
	cp.CreateFile();
	boost::filesystem::path f("test.h5");
	cp.OpenFile();
	cp.WriteHolidays(pt_config.get_child("run").get<std::string>("holidays_file", "holidays_flanders_2016.json"));
	cp.CloseFile();

	cp.OpenFile();
	Calendar c = cp.LoadCalendar(boost::gregorian::date(2017, 1, 1));
	cp.CloseFile();

	EXPECT_EQ(c.GetYear(), 2017U);
	EXPECT_EQ(c.GetMonth(), 1U);
	EXPECT_EQ(c.GetDay(), 1U);
}

TEST(CheckPoint, SaveLoadCheckPoint)
{
	boost::property_tree::ptree pt_config;
	util::InstallDirs::ReadXmlFile("config/run_test_save.xml", util::InstallDirs::GetRootDir(), pt_config);

	MultiSimulationConfig config;
	config.Parse(pt_config.get_child("run"));
	config.common_config->track_index_case = 0;

	std::string sim_output_prefix = "test";

	auto log_name = std::string("contact_logger_") + sim_output_prefix;
	auto file_logger = spdlog::rotating_logger_mt(
	    log_name, sim_output_prefix + "_logfile", std::numeric_limits<size_t>::max(),
	    std::numeric_limits<size_t>::max());
	file_logger->set_pattern("%v"); // Remove meta data from log => time-stamp of logging

	auto sim = SimulatorBuilder::Build(config.AsSingleConfig(), file_logger);

	stride::checkpoint::CheckPoint* cp = new stride::checkpoint::CheckPoint("SaveCheckPoint.h5");

	cp->CreateFile();
	cp->OpenFile();
	cp->SaveCheckPoint(*sim, 0);
	cp->WriteAtlas(sim->GetPopulation()->GetAtlas());
	cp->CloseFile();

	Simulator SimRead;

	cp->OpenFile();
	cp->LoadCheckPoint(sim->GetDate(), SimRead);
	cp->CloseFile();

	const auto& origPop = *sim->GetPopulation();
	ClusterStruct clOrig = sim->GetClusters();

	const auto& popRead = *SimRead.GetPopulation();
	auto clRead = SimRead.GetClusters();

	EXPECT_EQ(origPop.size(), popRead.size());
	EXPECT_EQ(origPop.get_infected_count(), popRead.get_infected_count());

	auto origPopptr = origPop.begin();
	auto popReadptr = popRead.begin();
	for (unsigned int i = 0; i < origPop.size(); i++) {

		Person origP = *origPopptr;
		Person readP = *popReadptr;

		EXPECT_EQ(origP.GetId(), readP.GetId());
		EXPECT_EQ(origP.GetAge(), readP.GetAge());
		EXPECT_EQ(origP.GetGender(), readP.GetGender());
		EXPECT_EQ(origP.IsParticipatingInSurvey(), readP.IsParticipatingInSurvey());

		EXPECT_EQ(origP.GetClusterId(ClusterType::Household), readP.GetClusterId(ClusterType::Household));
		EXPECT_EQ(origP.GetClusterId(ClusterType::School), readP.GetClusterId(ClusterType::School));
		EXPECT_EQ(origP.GetClusterId(ClusterType::Work), readP.GetClusterId(ClusterType::Work));
		EXPECT_EQ(
		    origP.GetClusterId(ClusterType::PrimaryCommunity),
		    readP.GetClusterId(ClusterType::PrimaryCommunity));
		EXPECT_EQ(
		    origP.GetClusterId(ClusterType::SecondaryCommunity),
		    readP.GetClusterId(ClusterType::SecondaryCommunity));

		Health origH = origP.GetHealth();
		Health readH = readP.GetHealth();

		EXPECT_EQ(origH.GetHealthStatus(), readH.GetHealthStatus());
		EXPECT_EQ(origH.GetStartInfectiousness(), readH.GetStartInfectiousness());
		EXPECT_EQ(origH.GetEndInfectiousness(), readH.GetEndInfectiousness());
		EXPECT_EQ(origH.GetStartSymptomatic(), readH.GetStartSymptomatic());
		EXPECT_EQ(origH.GetEndSymptomatic(), readH.GetEndSymptomatic());
		EXPECT_EQ(origH.GetDaysInfected(), readH.GetDaysInfected());

		origPopptr++;
		popReadptr++;
	}

	EXPECT_EQ(clOrig.m_households.size(), clRead.m_households.size());
	for (unsigned int i = 0; i < clOrig.m_households.size(); i++) {
		EXPECT_EQ(clOrig.m_households[i].GetSize(), clRead.m_households[i].GetSize());
		EXPECT_EQ(clOrig.m_households[i].GetId(), clRead.m_households[i].GetId());
		EXPECT_EQ(clOrig.m_households[i].GetClusterType(), clRead.m_households[i].GetClusterType());
		auto popClOrig = clOrig.m_households[i].GetPeople();
		auto popClRead = clRead.m_households[i].GetPeople();
		for (unsigned int j = 0; j < popClOrig.size(); j++) {
			EXPECT_EQ(popClOrig[j], popClRead[j]);
		}
	}

	EXPECT_EQ(clOrig.m_school_clusters.size(), clRead.m_school_clusters.size());
	for (unsigned int i = 0; i < clOrig.m_school_clusters.size(); i++) {
		EXPECT_EQ(clOrig.m_school_clusters[i].GetSize(), clRead.m_school_clusters[i].GetSize());
		EXPECT_EQ(clOrig.m_school_clusters[i].GetId(), clRead.m_school_clusters[i].GetId());
		EXPECT_EQ(clOrig.m_school_clusters[i].GetClusterType(), clRead.m_school_clusters[i].GetClusterType());
		auto popClOrig = clOrig.m_school_clusters[i].GetPeople();
		auto popClRead = clRead.m_school_clusters[i].GetPeople();
		for (unsigned int j = 0; j < popClOrig.size(); j++) {
			EXPECT_EQ(popClOrig[j], popClRead[j]);
		}
	}

	EXPECT_EQ(clOrig.m_work_clusters.size(), clRead.m_work_clusters.size());
	for (unsigned int i = 0; i < clOrig.m_work_clusters.size(); i++) {
		EXPECT_EQ(clOrig.m_work_clusters[i].GetSize(), clRead.m_work_clusters[i].GetSize());
		EXPECT_EQ(clOrig.m_work_clusters[i].GetId(), clRead.m_work_clusters[i].GetId());
		EXPECT_EQ(clOrig.m_work_clusters[i].GetClusterType(), clRead.m_work_clusters[i].GetClusterType());
		auto popClOrig = clOrig.m_work_clusters[i].GetPeople();
		auto popClRead = clRead.m_work_clusters[i].GetPeople();
		for (unsigned int j = 0; j < popClOrig.size(); j++) {
			EXPECT_EQ(popClOrig[j], popClRead[j]);
		}
	}

	EXPECT_EQ(clOrig.m_primary_community.size(), clRead.m_primary_community.size());
	for (unsigned int i = 0; i < clOrig.m_primary_community.size(); i++) {
		EXPECT_EQ(clOrig.m_primary_community[i].GetSize(), clRead.m_primary_community[i].GetSize());
		EXPECT_EQ(clOrig.m_primary_community[i].GetId(), clRead.m_primary_community[i].GetId());
		EXPECT_EQ(
		    clOrig.m_primary_community[i].GetClusterType(), clRead.m_primary_community[i].GetClusterType());
		auto popClOrig = clOrig.m_primary_community[i].GetPeople();
		auto popClRead = clRead.m_primary_community[i].GetPeople();
		for (unsigned int j = 0; j < popClOrig.size(); j++) {
			EXPECT_EQ(popClOrig[j], popClRead[j]);
		}
	}

	EXPECT_EQ(clOrig.m_secondary_community.size(), clRead.m_secondary_community.size());
	for (unsigned int i = 0; i < clOrig.m_secondary_community.size(); i++) {
		EXPECT_EQ(clOrig.m_secondary_community[i].GetSize(), clRead.m_secondary_community[i].GetSize());
		EXPECT_EQ(clOrig.m_secondary_community[i].GetId(), clRead.m_secondary_community[i].GetId());
		EXPECT_EQ(
		    clOrig.m_secondary_community[i].GetClusterType(), clRead.m_secondary_community[i].GetClusterType());
		auto popClOrig = clOrig.m_secondary_community[i].GetPeople();
		auto popClRead = clRead.m_secondary_community[i].GetPeople();
		for (unsigned int j = 0; j < popClOrig.size(); j++) {
			EXPECT_EQ(popClOrig[j], popClRead[j]);
		}
	}

	Atlas origAtlas = origPop.get_atlas();
	Atlas readAtlas = popRead.get_atlas();

	for (auto& key : origAtlas.cluster_map) {
		auto it = readAtlas.cluster_map.find(key.first);
		EXPECT_NE(it, readAtlas.cluster_map.end());
		EXPECT_EQ(key.second.latitude, it->second.latitude);
		EXPECT_EQ(key.second.longitude, it->second.longitude);
	}

	for (auto& key : origAtlas.town_map) {
		auto it = readAtlas.town_map.find(key.first);
		EXPECT_NE(it, readAtlas.town_map.end());
		EXPECT_EQ(key.second.id, it->second.id);
		EXPECT_EQ(key.second.size, it->second.size);
		EXPECT_EQ(key.second.name, it->second.name);
	}
}
} // Tests