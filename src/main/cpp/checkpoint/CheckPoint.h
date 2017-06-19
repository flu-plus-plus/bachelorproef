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
class Simulator;
struct ClusterStruct;
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
	/// The parameter is the MultiSimulationConfig you want to write
	void WriteConfig(const MultiSimulationConfig& conf);

	/// Writes the SingleSimulationConfig.
	/// The parameter is the SingleSimulationConfig you want to write
	void WriteConfig(const SingleSimulationConfig& conf);

	/// Loads all data from a checkpoint into a Simulator. It will not load the configuration.
	/// The first parameter is the date to load, the second parameter is the simulation to load into.
	void LoadCheckPoint(boost::gregorian::date date, Simulator& sim);

	/// Saves the current simulation to a checkpoint with the date as Identifier.
	/// The first parameter is the Simulator you save from, the second parameter is the day in the simulation.
	void SaveCheckPoint(const Simulator& simulation, std::size_t day);

	/// Copies the info in the filename under the data of the given simulation.
	/// The first parameter is the ID of the simulation you want to save, the second parameter is the name of the
	/// file subcheckpoint.
	void CombineCheckPoint(unsigned int simulation, const std::string& filename);

	/// Copies the info of the asked simulation into the given file.
	/// The first parameter is the ID of the simulation you want to load, the second parameter is the file you want
	/// to write it to.
	void SplitCheckPoint(unsigned int simulation, const std::string& filename);

	/// Loads the MultiConfig of a checkpoint. Can be used from a single or multicheckpoint.
	MultiSimulationConfig LoadMultiConfig();

	/// Loads a SingleSimulationConfig. This can only be used from a singular checkpoint.
	SingleSimulationConfig LoadSingleConfig();

	/// Writes the holidays to the checkpoint from a file.
	/// The parameter is the file to containing the info about the holidays.
	void WriteHolidays(const std::string& filename);

	/// Loads the Calendar starting with the given date.
	/// The parameter is the start day of the Calendar.
	Calendar LoadCalendar(boost::gregorian::date date);

	/// Search for the last date written.
	boost::gregorian::date GetLastDate();

	/// Writes the Atlas.
	/// The parameter is the Atlas to write.
	void WriteAtlas(const Atlas& atlas);

	/// Loads the disease in the given file.
	/// The parameter is the name of the file to write to.
	void LoadDisease(const std::string& filename);

	/// Stores the disease from the given file into the checkpoint.
	/// The parameter is the name of the file to load from.
	void StoreDisease(const std::string& filename);

	/// Loads the contact matrix in the given file.
	/// The parameter is the name of the file to write to.
	void LoadMatrix(const std::string& filename);

	/// Stores the contact matrix from the given file into the checkpoint.
	/// The parameter is the name of the file to load from.

	void StoreMatrix(const std::string& filename);

	/// Loads the changable config values in json-format in the file with given name.
	/// The parameter is the name of the file to write to.
	void LoadConfig(const std::string& filename);

	/// Stores the changable config values from the given file.
	/// The parameter is the name of the file to load from.
	void StoreConfig(const std::string& filename);

private:
	/// Writes the current population to a checkpoint.
	void WritePopulation(const Population& pop, boost::gregorian::date date);

	/// Writes the clusters to a checkpoint.
	void WriteClusters(const ClusterStruct& clusters, boost::gregorian::date date);

	/// Writes the visitors to a checkpoint.
	void WriteVisitors(multiregion::VisitorJournal& visitors, boost::gregorian::date date, std::size_t day day);

	/// Writes the expatriates to a checkpoint.
	void WriteExpatriates(multiregion::ExpatriateJournal& expatriates, boost::gregorian::date date);

	/// Loads one type Cluster
	void WriteCluster(const std::vector<Cluster>& clusters, hid_t& group, const ClusterType& type);

	/// Loads one type Cluster
	void LoadCluster(
	    std::vector<Cluster>& clusters, const ClusterType& type, const std::string& groupname,
	    const Population& pop);

	/// Loads the Expatriate journal
	multiregion::ExpatriateJournal LoadExpatriates(const Population& pop, boost::gregorian::date date);

	/// Loads the Visitor journal
	multiregion::VisitorJournal LoadVisitors(boost::gregorian::date date);

	/// Loads the Atlas directly into the population
	void LoadAtlas(Population& pop);

	/// Writes a file to a dataset. The first string is the filename in the data folder. The second string is the
	/// name of the dataset
	void WriteFileDSet(const std::string& filename, const std::string& setname);

	/// Writes a dataset to a file. The first string is the filename in the data folder. The second string is the
	/// name of the dataset
	std::string WriteDSetFile(const std::string& filename, const std::string& setname);

	/// This loads the data in the file in the first string in the dataset with the name of the second string
	/// This looks like WriteDSetFile, but does not load it in the datafolder.
	void LoadFile(const std::string& filename, const std::string& setname);

	/// This stores the data in the file in the first string in the dataset with the name of the second string
	/// This looks like WriteFileDSet, but the file is not neccesarily in the datafolder.
	void StoreFile(const std::string& filename, const std::string& setname);

	/// Writes the towns from the Atlas
	void WriteTowns(const Atlas& atlas);

	/// Loads the towns into the Atlas
	void LoadTowns(Population& pop);

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
		const char* name;
	};
};

} /* namespace checkpoint */
} /* namespace stride */

#endif /* CHECKPOINT_H_INCLUDED */
