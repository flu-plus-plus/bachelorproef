#ifndef CHECKPOINT_H_INCLUDED
#define CHECKPOINT_H_INCLUDED

#include <memory>
#include <vector>
#include <hdf5.h>
#include "calendar/Calendar.h"
#include "core/Cluster.h"
#include "pop/Population.h"
#include "sim/SimulationConfig.h"
#include "sim/Simulator.h"

namespace stride {
namespace checkpoint {

class CheckPoint
{
public:
	/// Constructor The string is the file for the checkpoints.
	CheckPoint(const std::string& filename);

	/// Creates the wanted file and immediately closes it. It will overwrite a file if one of the same name already
	/// exists.
	void CreateFile();

	/// Opens the wanted file. This is necessary for any of the following functions.
	void OpenFile();

	/// Closes the wanted file. This is necessary for any of the following functions.
	void CloseFile();

	/// Writes the MultiSimulationConfig in different simulation groups if necessary.
	void WriteConfig(const MultiSimulationConfig& conf);

	/// Writes the SingleSimulationConfig.
	void WriteConfig(const SingleSimulationConfig& conf);

	/// Loads all data from a checkpoint into a Simulator. It will not load the configuration.
	void LoadCheckPoint(boost::gregorian::date date, Simulator& sim);

	/// Saves the current simulation to a checkpoint with the date as Identifier.
	void SaveCheckPoint(const Simulator& simulation, std::size_t day);

	/// Copies the info in the filename under the data of the given simulation
	void CombineCheckPoint(unsigned int simulation, const std::string& filename);

	/// Copies the info of the asked simulation into the given file
	void SplitCheckPoint(unsigned int simulation, const std::string& filename);

	/// Loads a SingleSimulationConfig.
	SingleSimulationConfig LoadSingleConfig();

	/// Writes the holidays from a file. The int pointer represents the group.
	void WriteHolidays(const std::string& filename, unsigned int* simulation = NULL);

	/// Loads the Calendar starting with the given date.
	Calendar LoadCalendar(boost::gregorian::date date);

	/// Search for the last date written.
	boost::gregorian::date GetLastDate();

	/// Writes the Atlas
	void WriteAtlas(const Atlas&);

private:
	/// Writes the current population to a checkpoint.
	void WritePopulation(const Population&, boost::gregorian::date);

	/// Writes the clusters to a checkpoint.
	void WriteClusters(const ClusterStruct&, boost::gregorian::date);

	/// Writes the visitors to a checkpoint.
	void WriteVisitors(multiregion::VisitorJournal&, boost::gregorian::date, std::size_t day);

	/// Writes the expatriates to a checkpoint.
	void WriteExpatriates(multiregion::ExpatriateJournal&, boost::gregorian::date);

	/// Loads one type Cluster
	void WriteCluster(const std::vector<Cluster>&, hid_t&, const ClusterType&);

	/// Loads one type Cluster
	void LoadCluster(std::vector<Cluster>&, const ClusterType&, const std::string& groupname, const Population&);

	/// Loads the Expatriate journal
	multiregion::ExpatriateJournal LoadExpatriates(const Population&, boost::gregorian::date);

	/// Loads the Visitor journal
	multiregion::VisitorJournal LoadVisitors(boost::gregorian::date);

	/// Loads the Atlas directly into the population
	void LoadAtlas(Population&);

	/// Writes a file to a dataset. The first string is the filename in the data folder. The second string is the
	/// name of the dataset
	void WriteFileDSet(const std::string&, const std::string&);

	/// Writes a dataset to a file. The first string is the filename in the data folder. The second string is the
	/// name of the dataset
	std::string WriteDSetFile(const std::string&, const std::string&);

	/// Writes the towns from the Atlas
	void WriteTowns(const Atlas&);

	/// Loads the towns into the Atlas
	void LoadTowns(Atlas&);

	hid_t m_file;		      //< current hdf5 workspace
	const std::string m_filename; //< filename

	struct h_personType
	{
		// basic info
		unsigned int ID;
		double Age;
		char Gender;
		hbool_t Participating;
		// health info
		hbool_t Immune;
		hbool_t Infected;
		unsigned int StartInf;
		unsigned int EndInf;
		unsigned int StartSympt;
		unsigned int EndSympt;
		unsigned int TimeInfected;
		// cluster info
		unsigned int Household;
		unsigned int School;
		unsigned int Work;
		unsigned int Primary;
		unsigned int Secondary;
		h_personType(const Person& p)
		{
			ID = p.GetId();
			Age = p.GetAge();
			Gender = p.GetGender();
			Participating = p.IsParticipatingInSurvey();
			Immune = p.GetHealth().IsImmune();
			Infected = p.GetHealth().IsInfected();
			StartInf = p.GetHealth().GetStartInfectiousness();
			EndInf = p.GetHealth().GetEndInfectiousness();
			StartSympt = p.GetHealth().GetStartSymptomatic();
			EndSympt = p.GetHealth().GetEndSymptomatic();
			TimeInfected = p.GetHealth().GetDaysInfected();

			Household = p.GetClusterId(ClusterType::Household);
			School = p.GetClusterId(ClusterType::School);
			Work = p.GetClusterId(ClusterType::Work);
			Primary = p.GetClusterId(ClusterType::PrimaryCommunity);
			Secondary = p.GetClusterId(ClusterType::SecondaryCommunity);
		}
		h_personType() {}
	};

	struct h_clusterType
	{
		unsigned int ID;
		unsigned int PersonID;
	};

	struct h_visitorType
	{
		unsigned int DaysLeft;
		unsigned int RegionID;
		unsigned int PersonIDHome;
		unsigned int PersonIDVisitor;
	};

	struct h_clusterAtlas
	{
		unsigned int ClusterID;
		unsigned int ClusterType;
		double latitude;
		double longitude;
	};

	struct h_town
	{	
		double latitude;
		double longitude;
		unsigned int size;
		unsigned int id;
		std::string name;
	};
};

} /* namespace checkpoint */
} /* namespace stride */

#endif /* CHECKPOINT_H_INCLUDED */
