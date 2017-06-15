#include <hdf5.h>
#include "CheckPoint.h"

#include <calendar/Calendar.h>
#include <core/ClusterType.h>
#include <core/Health.h>
#include <multiregion/TravelModel.h>
#include <pop/Person.h>
#include <util/Errors.h>
#include <util/InstallDirs.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>

using namespace std;

namespace stride {
namespace checkpoint {

CheckPoint::CheckPoint(const std::string& filename) : m_filename(filename) {}

void CheckPoint::CreateFile()
{
	m_file = H5Fcreate(m_filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	H5Fclose(m_file);
}

void CheckPoint::CloseFile() { H5Fclose(m_file); }

void CheckPoint::WriteConfig(const SingleSimulationConfig& conf)
{
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, "Config", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}

	std::shared_ptr<CommonSimulationConfig> common_config = conf.common_config;
	std::shared_ptr<LogConfig> log_config = conf.log_config;

	WriteFileDSet(common_config->disease_config_file_name, "disease");
	WriteFileDSet(common_config->contact_matrix_file_name, "contact");
	WriteFileDSet(conf.GetPopulationPath(), "popconfig");
	if (conf.GetGeodistributionProfilePath().size() != 0) {
		WriteFileDSet(conf.GetGeodistributionProfilePath(), "geoconfig");
	}
	if (conf.GetReferenceHouseholdsPath().size() != 0) {
		WriteFileDSet(conf.GetReferenceHouseholdsPath(), "household");
	}

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);
	hsize_t dims = 3;
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);
	hbool_t bools[3];
	bools[0] = common_config->track_index_case;
	bools[1] = log_config->generate_person_file;
	// should always be true
	bools[2] = common_config->use_checkpoint;
	hid_t attr = H5Acreate2(group, "bools", H5T_NATIVE_HBOOL, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_HBOOL, bools);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = 6;
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "uints", H5T_NATIVE_UINT, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	unsigned int uints[6];
	uints[0] = common_config->rng_seed;
	uints[1] = common_config->number_of_days;
	uints[2] = common_config->number_of_survey_participants;
	uints[3] = (unsigned int)log_config->log_level;
	uints[4] = conf.GetId();
	uints[5] = common_config->checkpoint_interval;
	H5Awrite(attr, H5T_NATIVE_UINT, uints);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = 3;
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "doubles", H5T_NATIVE_DOUBLE, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	double doubles[3];
	doubles[0] = common_config->r0;
	doubles[1] = common_config->seeding_rate;
	doubles[2] = common_config->immunity_rate;

	H5Awrite(attr, H5T_NATIVE_DOUBLE, doubles);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = log_config->output_prefix.size();
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "prefix", H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_CHAR, log_config->output_prefix.c_str());
	H5Sclose(dataspace);
	H5Aclose(attr);

	H5Gclose(group);
}

void CheckPoint::WriteConfig(const MultiSimulationConfig& conf)
{
	hid_t f = m_file;
	auto singles = conf.GetSingleConfigs();
	if (singles.size() == 1) {
		WriteConfig(singles[0]);
		return;
	}
	for (unsigned int i = 0; i < singles.size(); i++) {
		std::stringstream ss;
		ss << "Simulation " << i;

		m_file = H5Gcreate2(m_file, ss.str().c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		WriteConfig(singles[i]);
		H5Gclose(m_file);
	}
	m_file = f;
}

void CheckPoint::OpenFile() { m_file = H5Fopen(m_filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT); }

void CheckPoint::WriteHolidays(const std::string& filename, unsigned int* group)
{
	hid_t temp = m_file;
	if (group != nullptr) {
		std::stringstream ss;
		ss << "Simulation " << *group;
		m_file = H5Gopen2(m_file, ss.str().c_str(), H5P_DEFAULT);
	}
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		hid_t tempCreate = H5Gcreate2(m_file, "Config", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(tempCreate);
	}
	WriteFileDSet(filename, "holidays");
	if (group != nullptr) {
		H5Gclose(m_file);
		m_file = temp;
	}
}

void CheckPoint::WritePopulation(const Population& pop, boost::gregorian::date date)
{
	std::string datestr = to_iso_string(date);
	htri_t exist = H5Lexists(m_file, datestr.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, datestr.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}

	hsize_t dims[1] = {pop.size()};
	hid_t dataspace = H5Screate_simple(1, dims, nullptr);
	std::string spot = datestr + "/Population";

	hid_t chunkP = H5Pcreate(H5P_DATASET_CREATE);
	hsize_t chunkDims[1] = {1};
	H5Pset_chunk(chunkP, 1, chunkDims);

	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_personType));

	H5Tinsert(newType, "ID", HOFFSET(h_personType, ID), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Age", HOFFSET(h_personType, Age), H5T_NATIVE_DOUBLE);
	H5Tinsert(newType, "Gender", HOFFSET(h_personType, Gender), H5T_NATIVE_CHAR);
	H5Tinsert(newType, "Participating", HOFFSET(h_personType, Participating), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "Immune", HOFFSET(h_personType, Immune), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "Infected", HOFFSET(h_personType, Infected), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "StartInf", HOFFSET(h_personType, StartInf), H5T_NATIVE_UINT);
	H5Tinsert(newType, "EndInf", HOFFSET(h_personType, EndInf), H5T_NATIVE_UINT);
	H5Tinsert(newType, "StartSympt", HOFFSET(h_personType, StartSympt), H5T_NATIVE_UINT);
	H5Tinsert(newType, "EndSympt", HOFFSET(h_personType, EndSympt), H5T_NATIVE_UINT);
	H5Tinsert(newType, "TimeInfected", HOFFSET(h_personType, TimeInfected), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Household", HOFFSET(h_personType, Household), H5T_NATIVE_UINT);
	H5Tinsert(newType, "School", HOFFSET(h_personType, School), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Work", HOFFSET(h_personType, Work), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Primary", HOFFSET(h_personType, Primary), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Secondary", HOFFSET(h_personType, Secondary), H5T_NATIVE_UINT);

	hid_t dataset = H5Dcreate2(m_file, spot.c_str(), newType, dataspace, H5P_DEFAULT, chunkP, H5P_DEFAULT);

	chunkP = H5Pcreate(H5P_DATASET_XFER);
	unsigned int i = 0;

	pop.serial_for([this, &i, &newType, &dataspace, &dataset, &chunkP](const Person& p, unsigned int) {
		h_personType data(p);

		hsize_t start[1] = {i};
		hsize_t count[1] = {1};
		hid_t chunkspace = H5Screate_simple(1, count, nullptr);
		H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, start, nullptr, count, nullptr);
		H5Dwrite(dataset, newType, chunkspace, dataspace, chunkP, &data);
		i++;
		H5Sclose(chunkspace);
	});
	H5Tclose(newType);
	H5Dclose(dataset);
	H5Sclose(dataspace);
}

void CheckPoint::WriteFileDSet(const std::string& filename, const std::string& setname)
{
	boost::filesystem::path path = util::InstallDirs::GetDataDir();
	boost::filesystem::path filep(filename);
	boost::filesystem::path fullpath = path / filep;
	if (!is_regular_file(fullpath)) {
		FATAL_ERROR("Unable to find file: " + fullpath.string());
	}
	std::string extension = filep.extension().string();
	std::ifstream f(fullpath.string());

	std::vector<char> dataset{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);
	hid_t dset;
	if (H5Lexists(group, setname.c_str(), H5P_DEFAULT) <= 0) {
		hsize_t dims[1] = {dataset.size()};
		hid_t dataspace = H5Screate_simple(1, dims, nullptr);
		dset = H5Dcreate2(
		    group, setname.c_str(), H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Sclose(dataspace);
	} else {
		dset = H5Dopen2(group, setname.c_str(), H5P_DEFAULT);
	}
	H5Dwrite(dset, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, dataset.data());
	if (H5Aexists(dset, "extension") > 0) {
		H5Adelete(dset, "extension");
	}
	hsize_t attrDims = extension.size();
	hid_t dataspace = H5Screate_simple(1, &attrDims, nullptr);
	hid_t attr = H5Acreate2(dset, "extension", H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_CHAR, extension.c_str());
	H5Aclose(attr);

	H5Dclose(dset);
	H5Sclose(dataspace);
	H5Gclose(group);
}

std::string CheckPoint::WriteDSetFile(const std::string& filestr, const std::string& setname)
{

	htri_t exist = H5Lexists(m_file, setname.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}

	auto path = util::InstallDirs::GetDataDir();

	boost::filesystem::path filename(filestr);
	/*
	Piece of code based on code on
	https://lists.hdfgroup.org/pipermail/hdf-forum_lists.hdfgroup.org/2010-June/003208.html
	*/
	std::vector<char> data;
	hid_t did;
	herr_t err = 0;
	hid_t spaceId;
	hid_t dataType = H5T_NATIVE_CHAR;

	// std::cout << "HDF5 Data Type: " <<	H5Lite::HDFTypeForPrimitiveAsStr(test) << std::endl;
	/* Open the dataset. */
	// std::cout << "  Opening " << dsetName << " for data Retrieval.  "<< std::endl;
	did = H5Dopen(m_file, setname.c_str(), H5P_DEFAULT);

	if (did < 0) {
		std::cout << " Error opening Dataset: " << did << std::endl;
		return "";
	}

	std::string extension;
	hid_t attr = H5Aopen(did, "extension", H5P_DEFAULT);

	std::unique_ptr<H5A_info_t> info = std::make_unique<H5A_info_t>();

	H5Aget_info(attr, info.get());
	std::vector<char> extensionVector(info->data_size, '\0');
	H5Aread(attr, H5T_NATIVE_CHAR, extensionVector.data());
	H5Aclose(attr);

	extension = std::string(extensionVector.begin(), extensionVector.end());

	filename.replace_extension(extension);

	std::ofstream out((path / filename).string(), std::ios::out | std::ios::trunc);
	if (did >= 0) {
		spaceId = H5Dget_space(did);
		if (spaceId > 0) {
			int32_t rank = H5Sget_simple_extent_ndims(spaceId);
			if (rank > 0) {
				std::vector<hsize_t> dims;
				dims.resize(rank); // Allocate enough room for the dims
				rank = H5Sget_simple_extent_dims(spaceId, &(dims.front()), NULL);
				hsize_t numElements = 1;
				for (std::vector<hsize_t>::iterator iter = dims.begin(); iter < dims.end(); ++iter) {
					numElements = numElements * (*iter);
				}
				// std::cout << "NumElements: " << numElements << std::endl;
				// Resize the vector
				data.resize(static_cast<int>(numElements));
				// for (uint32_t i = 0; i<numElements; ++i) { data[i] = 55555555;}
				err = H5Dread(did, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, &(data.front()));
				if (err < 0) {
					std::cout << "Error Reading Data." << std::endl;
				}
			}
			err = H5Sclose(spaceId);
			if (err < 0) {
				std::cout << "Error Closing Data Space" << std::endl;
			}
		} else {
			std::cout << "Error Opening SpaceID" << std::endl;
		}
		err = H5Dclose(did);
		if (err < 0) {
			std::cout << "Error Closing Dataset" << std::endl;
		}
		for (auto& c : data) {
			out << c;
		}
	}
	return filename.string();
}

void CheckPoint::SaveCheckPoint(const Simulator& sim, std::size_t day)
{
	// TODO: add airport
	WritePopulation(*sim.GetPopulation(), sim.GetDate());
	WriteClusters(sim.GetClusters(), sim.GetDate());
	auto exp = sim.GetExpatriateJournal();
	WriteExpatriates(exp, sim.GetDate());
	auto vis = sim.GetVistiorJournal();
	WriteVisitors(vis, sim.GetDate(), day);
}

void CheckPoint::CombineCheckPoint(unsigned int groupnum, const std::string& filename)
{
	std::stringstream ss;
	ss << "Simulation " << groupnum;
	std::string groupname = ss.str();

	htri_t exist = H5Lexists(m_file, groupname.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, groupname.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}

	hid_t group = H5Gopen2(m_file, groupname.c_str(), H5P_DEFAULT);

	hid_t newFile = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);

	auto op_func = [&group](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		H5Ocopy(loc_id, name, group, name, H5P_DEFAULT, H5P_DEFAULT);
		return 0;
	};
	auto temp = [](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		(*static_cast<decltype(op_func)*>(operator_data))(loc_id, name, info, operator_data);
		return 0;
	};

	H5Literate(newFile, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr, temp, &op_func);

	H5Fclose(newFile);
	H5Gclose(group);
}

void CheckPoint::LoadCheckPoint(boost::gregorian::date date, Simulator& sim)
{

	std::string groupname = to_iso_string(date);
	std::string name = groupname + "/Population";
	htri_t exist = H5Lexists(m_file, name.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Incorrect date loaded");
	}
	// loading people
	hid_t dset = H5Dopen(m_file, name.c_str(), H5P_DEFAULT);
	hid_t dspace = H5Dget_space(dset);
	hid_t newType = H5Dget_type(dset);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	Population result;

	for (hsize_t i = 0; i < dims; i++) {
		hsize_t start = i;

		hsize_t count = 1;

		hid_t subspace = H5Screate_simple(1, &count, nullptr);

		H5Sselect_hyperslab(dspace, H5S_SELECT_SET, &start, nullptr, &count, nullptr);

		h_personType data;

		H5Dread(dset, newType, subspace, dspace, H5P_DEFAULT, &data);

		disease::Fate disease;
		disease.start_infectiousness = data.StartInf;
		disease.start_symptomatic = data.StartSympt;
		disease.end_infectiousness = data.EndInf;
		disease.end_symptomatic = data.EndSympt;

		Person toAdd(
		    data.ID, data.Age, data.Household, data.School, data.Work, data.Primary, data.Secondary, disease);

		if (data.Participating) {
			toAdd.ParticipateInSurvey();
		}

		if (data.Immune) {
			toAdd.GetHealth().SetImmune();
		}
		if (data.Infected) {
			toAdd.GetHealth().StartInfection();
		}
		for (unsigned int i = 0; i < data.TimeInfected; i++) {
			toAdd.GetHealth().Update();
		}

		result.emplace(toAdd);
		H5Sclose(subspace);
	}

	H5Sclose(dspace);
	H5Tclose(newType);
	H5Dclose(dset);

	LoadAtlas(result);

	sim.SetPopulation(result);
	sim.SetExpatriates(LoadExpatriates(result, date));
	sim.SetVisitors(LoadVisitors(date));

	ClusterStruct clusters;
	// loading clusters
	LoadCluster(sim.GetClusters().m_households, ClusterType::Household, groupname, result);
	LoadCluster(sim.GetClusters().m_school_clusters, ClusterType::School, groupname, result);
	LoadCluster(sim.GetClusters().m_work_clusters, ClusterType::Work, groupname, result);
	LoadCluster(sim.GetClusters().m_primary_community, ClusterType::PrimaryCommunity, groupname, result);
	LoadCluster(sim.GetClusters().m_secondary_community, ClusterType::SecondaryCommunity, groupname, result);
}

void CheckPoint::LoadCluster(
    std::vector<Cluster>& clusters, const ClusterType& i, const std::string& groupname, const Population& result)
{
	clusters.clear();
	std::string type = ToString(i);
	std::string path = groupname + "/" + type;
	hid_t clusterID = H5Dopen2(m_file, path.c_str(), H5P_DEFAULT);
	hid_t dspace = H5Dget_space(clusterID);
	hid_t newType = H5Dget_type(clusterID);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	hsize_t start = 0;

	hsize_t count = 1;

	hid_t subspace = H5Dget_space(clusterID);
	H5Sselect_hyperslab(subspace, H5S_SELECT_SET, &start, nullptr, &count, nullptr);

	h_clusterType data;

	H5Dread(clusterID, newType, H5S_ALL, subspace, H5P_DEFAULT, &data);

	H5Sclose(subspace);

	std::unique_ptr<Cluster> CurrentCluster = std::make_unique<Cluster>(data.ID, i);

	for (hsize_t j = 1; j < dims; j++) {
		hsize_t start = j;
		hsize_t count = 1;

		hid_t subspace = H5Screate_simple(1, &count, nullptr);

		H5Sselect_hyperslab(dspace, H5S_SELECT_SET, &start, nullptr, &count, nullptr);

		h_clusterType data;

		H5Dread(clusterID, newType, subspace, dspace, H5P_DEFAULT, &data);

		H5Sclose(subspace);

		if (data.ID != CurrentCluster->GetId()) {
			clusters.emplace_back(*CurrentCluster);
			CurrentCluster = std::make_unique<Cluster>(data.ID, i);
			continue;
		}

		unsigned int idPersonToAdd = data.PersonID;

		CurrentCluster->AddPerson(result.getPerson(idPersonToAdd));
	}

	clusters.emplace_back(*CurrentCluster);
	H5Tclose(newType);
	H5Dclose(clusterID);
	H5Sclose(dspace);
}

SingleSimulationConfig CheckPoint::LoadSingleConfig()
{
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}

	SingleSimulationConfig result;
	auto comCon = std::make_shared<CommonSimulationConfig>();
	auto logCon = std::make_shared<LogConfig>();
	result.common_config = comCon;
	result.log_config = logCon;

	std::string changed = WriteDSetFile("tmp_matrix.xml", "Config/contact");
	result.common_config->contact_matrix_file_name = changed;

	changed = WriteDSetFile("tmp_disease.xml", "Config/disease");
	result.common_config->disease_config_file_name = changed;

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);

	hid_t attr = H5Aopen(group, "bools", H5P_DEFAULT);
	hbool_t bools[3];
	H5Aread(attr, H5T_NATIVE_HBOOL, bools);
	H5Aclose(attr);

	result.common_config->track_index_case = bools[0];
	result.log_config->generate_person_file = bools[1];
	result.common_config->use_checkpoint = bools[2];

	attr = H5Aopen(group, "doubles", H5P_DEFAULT);
	double doubles[3];
	H5Aread(attr, H5T_NATIVE_DOUBLE, doubles);
	H5Aclose(attr);

	result.common_config->r0 = doubles[0];
	result.common_config->seeding_rate = doubles[1];
	result.common_config->immunity_rate = doubles[2];

	attr = H5Aopen(group, "uints", H5P_DEFAULT);
	unsigned int uints[6];
	H5Aread(attr, H5T_NATIVE_UINT, uints);
	H5Aclose(attr);

	result.common_config->rng_seed = uints[0];
	result.common_config->number_of_days = uints[1];
	result.common_config->number_of_survey_participants = uints[2];
	result.log_config->log_level = (LogMode)uints[3];
	unsigned int id = uints[4];
	result.common_config->checkpoint_interval = uints[5];

	attr = H5Aopen(group, "prefix", H5P_DEFAULT);

	std::unique_ptr<H5A_info_t> info = std::make_unique<H5A_info_t>();

	H5Aget_info(attr, info.get());
	std::vector<char> prefix(info->data_size, '\0');
	H5Aread(attr, H5T_NATIVE_CHAR, prefix.data());
	H5Aclose(attr);
	result.log_config->output_prefix = std::string(prefix.begin(), prefix.end());

	if (info->data_size == 0) {
		result.log_config->output_prefix = "";
	}

	// travel model

	changed = WriteDSetFile("tmp_pop.csv", "Config/popconfig");
	std::string popconfig = changed;
	std::string geoconfig = "";

	exist = H5Lexists(m_file, "Config/geoconfig", H5P_DEFAULT);
	if (exist > 0) {
		geoconfig = WriteDSetFile("tmp_geoconfig.xml", "Config/geoconfig");
	}
	std::string household = "";
	exist = H5Lexists(m_file, "Config/household", H5P_DEFAULT);
	if (exist > 0) {
		household = WriteDSetFile("tmp_household.xml", "Config/household");
	}
	auto travelRef = make_shared<multiregion::RegionTravel>(id, popconfig, geoconfig, household);
	result.travel_model = travelRef;

	return result;
}

void CheckPoint::SplitCheckPoint(unsigned int groupnum, const std::string& filename)
{
	std::stringstream ss;
	ss << "Simulation " << groupnum;
	std::string groupname = ss.str();

	htri_t exist = H5Lexists(m_file, groupname.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Non-existing group");
	}

	hid_t group = H5Gopen2(m_file, groupname.c_str(), H5P_DEFAULT);

	hid_t f = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

	auto op_func = [&f](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		H5Ocopy(loc_id, name, f, name, H5P_DEFAULT, H5P_DEFAULT);
		return 0;
	};
	auto temp = [](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		(*static_cast<decltype(op_func)*>(operator_data))(loc_id, name, info, operator_data);
		return 0;
	};

	H5Literate(group, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr, temp, &op_func);

	H5Fclose(f);
	H5Gclose(group);
}

Calendar CheckPoint::LoadCalendar(boost::gregorian::date date)
{
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}

	WriteDSetFile("tmp_holidays.json", "Config/holidays");

	Calendar result;
	result.Initialize(date, "tmp_holidays.json");
	boost::filesystem::remove("tmp_holidays.json");
	return result;
}

void CheckPoint::WriteClusters(const ClusterStruct& clusters, boost::gregorian::date date)
{

	std::string datestr = to_iso_string(date);
	htri_t exist = H5Lexists(m_file, datestr.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, datestr.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}
	hid_t group = H5Gopen2(m_file, datestr.c_str(), H5P_DEFAULT);

	WriteCluster(clusters.m_households, group, ClusterType::Household);
	WriteCluster(clusters.m_school_clusters, group, ClusterType::School);
	WriteCluster(clusters.m_work_clusters, group, ClusterType::Work);
	WriteCluster(clusters.m_primary_community, group, ClusterType::PrimaryCommunity);
	WriteCluster(clusters.m_secondary_community, group, ClusterType::SecondaryCommunity);

	H5Gclose(group);
}

void CheckPoint::WriteCluster(const std::vector<Cluster>& clvector, hid_t& group, const ClusterType& t)
{
	std::string dsetname = ToString(t);

	unsigned int totalSize = 0;
	std::vector<unsigned int> sizes;

	for (auto& cluster : clvector) {
		totalSize += cluster.GetSize() + 1;
		sizes.push_back(cluster.GetSize());
	}
	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_clusterType));

	H5Tinsert(newType, "ID", HOFFSET(h_clusterType, ID), H5T_NATIVE_UINT);
	H5Tinsert(newType, "PersonID", HOFFSET(h_clusterType, PersonID), H5T_NATIVE_UINT);

	hsize_t dims = totalSize;
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);

	hid_t dataset = H5Dcreate2(group, dsetname.c_str(), newType, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	unsigned int spot = 0;
	for (auto& cluster : clvector) {
		auto people = cluster.GetPeople();

		std::vector<h_clusterType> data{cluster.GetSize() + 1};

		// Start of new cluster
		data[0].ID = cluster.GetId();
		data[0].PersonID = 0;
		// Data in cluster
		for (unsigned int i = 1; i <= people.size(); i++) {
			data[i].ID = cluster.GetId();
			data[i].PersonID = people[i - 1].GetId();
		}

		hsize_t start = spot;
		hsize_t count = cluster.GetSize() + 1;
		hid_t chunkspace = H5Screate_simple(1, &count, nullptr);
		H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, &start, nullptr, &count, nullptr);

		hid_t plist = H5Pcreate(H5P_DATASET_XFER);

		H5Dwrite(dataset, newType, chunkspace, dataspace, plist, data.data());
		spot += cluster.GetSize() + 1;
		H5Sclose(chunkspace);
		H5Pclose(plist);
	}
	H5Tclose(newType);
	H5Sclose(dataspace);
	H5Dclose(dataset);
}

void CheckPoint::WriteExpatriates(multiregion::ExpatriateJournal& journal, boost::gregorian::date date)
{
	std::string datestr = to_iso_string(date);
	htri_t exist = H5Lexists(m_file, datestr.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, datestr.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}
	hid_t group = H5Gopen2(m_file, datestr.c_str(), H5P_DEFAULT);

	std::string dsetname = "Expatriates";

	std::vector<h_personType> data;

	journal.SerialForeach([&data](const Person& p, unsigned int) {
		h_personType tempPerson(p);
		data.push_back(tempPerson);
	});

	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_personType));

	H5Tinsert(newType, "ID", HOFFSET(h_personType, ID), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Age", HOFFSET(h_personType, Age), H5T_NATIVE_DOUBLE);
	H5Tinsert(newType, "Gender", HOFFSET(h_personType, Gender), H5T_NATIVE_CHAR);
	H5Tinsert(newType, "Participating", HOFFSET(h_personType, Participating), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "Immune", HOFFSET(h_personType, Immune), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "Infected", HOFFSET(h_personType, Infected), H5T_NATIVE_HBOOL);
	H5Tinsert(newType, "StartInf", HOFFSET(h_personType, StartInf), H5T_NATIVE_UINT);
	H5Tinsert(newType, "EndInf", HOFFSET(h_personType, EndInf), H5T_NATIVE_UINT);
	H5Tinsert(newType, "StartSympt", HOFFSET(h_personType, StartSympt), H5T_NATIVE_UINT);
	H5Tinsert(newType, "EndSympt", HOFFSET(h_personType, EndSympt), H5T_NATIVE_UINT);
	H5Tinsert(newType, "TimeInfected", HOFFSET(h_personType, TimeInfected), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Household", HOFFSET(h_personType, Household), H5T_NATIVE_UINT);
	H5Tinsert(newType, "School", HOFFSET(h_personType, School), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Work", HOFFSET(h_personType, Work), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Primary", HOFFSET(h_personType, Primary), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Secondary", HOFFSET(h_personType, Secondary), H5T_NATIVE_UINT);

	hsize_t dims = data.size();
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);

	hid_t dataset = H5Dcreate2(group, dsetname.c_str(), newType, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dataset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Gclose(group);
}

void CheckPoint::WriteVisitors(multiregion::VisitorJournal& journal, boost::gregorian::date date, std::size_t day)
{
	std::string datestr = to_iso_string(date);
	htri_t exist = H5Lexists(m_file, datestr.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, datestr.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}
	hid_t group = H5Gopen2(m_file, datestr.c_str(), H5P_DEFAULT);

	std::string dsetname = "Visitors";

	std::vector<h_visitorType> data(journal.GetVisitorCount());

	for (auto& days : journal.GetVisitors()) {
		for (auto& place : days.second) {
			for (auto p : place.second) {
				h_visitorType i;
				i.DaysLeft = days.first - day;
				i.RegionID = place.first;
				i.PersonIDHome = p.home_id;
				i.PersonIDVisitor = p.visitor_id;
				data.push_back(i);
			}
		}
	}

	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_visitorType));

	H5Tinsert(newType, "DaysLeft", HOFFSET(h_visitorType, DaysLeft), H5T_NATIVE_UINT);
	H5Tinsert(newType, "RegionID", HOFFSET(h_visitorType, RegionID), H5T_NATIVE_UINT);
	H5Tinsert(newType, "PersonIDHome", HOFFSET(h_visitorType, PersonIDHome), H5T_NATIVE_UINT);
	H5Tinsert(newType, "PersonIDVisitor", HOFFSET(h_visitorType, PersonIDVisitor), H5T_NATIVE_UINT);

	hsize_t dims = data.size();
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);

	hid_t dataset = H5Dcreate2(group, dsetname.c_str(), newType, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dataset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Tclose(newType);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Gclose(group);
}

multiregion::ExpatriateJournal CheckPoint::LoadExpatriates(const Population& pop, boost::gregorian::date date)
{
	multiregion::ExpatriateJournal result;
	std::string dsetName = to_iso_string(date) + "/Expatriates";
	htri_t exist = H5Lexists(m_file, dsetName.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		return result;
	}
	hid_t dset = H5Dopen2(m_file, dsetName.c_str(), H5P_DEFAULT);

	hid_t dspace = H5Dget_space(dset);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	hid_t newType = H5Dget_type(dset);

	std::vector<h_personType> data(dims);

	H5Dread(dset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Sclose(dspace);
	H5Dclose(dset);
	H5Tclose(newType);

	for (auto& p : data) {

		disease::Fate disease;
		disease.start_infectiousness = p.StartInf;
		disease.start_symptomatic = p.StartSympt;
		disease.end_infectiousness = p.EndInf;
		disease.end_symptomatic = p.EndSympt;

		Person toAdd(p.ID, p.Age, p.Household, p.School, p.Work, p.Primary, p.Secondary, disease);

		if (p.Participating) {
			toAdd.ParticipateInSurvey();
		}

		if (p.Immune) {
			toAdd.GetHealth().SetImmune();
		}
		if (p.Infected) {
			toAdd.GetHealth().StartInfection();
		}
		for (unsigned int i = 0; i < p.TimeInfected; i++) {
			toAdd.GetHealth().Update();
		}
		result.AddExpatriate(toAdd);
	}
	return result;
}

multiregion::VisitorJournal CheckPoint::LoadVisitors(boost::gregorian::date date)
{
	multiregion::VisitorJournal result;
	std::string dsetName = to_iso_string(date) + "/Visitors";
	htri_t exist = H5Lexists(m_file, dsetName.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		return result;
	}
	hid_t dset = H5Dopen2(m_file, dsetName.c_str(), H5P_DEFAULT);

	hid_t dspace = H5Dget_space(dset);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	hid_t newType = H5Dget_type(dset);

	std::vector<h_visitorType> data(dims);

	H5Dread(dset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Sclose(dspace);
	H5Dclose(dset);
	H5Tclose(newType);

	for (auto& p : data) {
		multiregion::VisitorId id;
		id.home_id = p.PersonIDHome;
		id.visitor_id = p.PersonIDVisitor;

		result.AddVisitor(id, p.RegionID, p.DaysLeft);
	}

	return result;
}

void CheckPoint::WriteAtlas(const Atlas& atlas)
{
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, "Config", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}

	std::vector<h_clusterAtlas> data;
	for (auto& i : atlas.cluster_map) {
		h_clusterAtlas info;
		info.ClusterID = i.first.first;
		info.ClusterType = (unsigned int)i.first.second;

		info.latitude = i.second.latitude;
		info.longitude = i.second.longitude;

		data.push_back(info);
	}

	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_clusterAtlas));

	H5Tinsert(newType, "ClusterID", HOFFSET(h_clusterAtlas, ClusterID), H5T_NATIVE_UINT);
	H5Tinsert(newType, "ClusterType", HOFFSET(h_clusterAtlas, ClusterType), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Latitude", HOFFSET(h_clusterAtlas, latitude), H5T_NATIVE_DOUBLE);
	H5Tinsert(newType, "Longitude", HOFFSET(h_clusterAtlas, longitude), H5T_NATIVE_DOUBLE);

	hsize_t dims = data.size();
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);

	hid_t dataset = H5Dcreate2(m_file, "Config/Atlas", newType, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dataset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Tclose(newType);
	H5Sclose(dataspace);
	H5Dclose(dataset);

	WriteTowns(atlas);
}

void CheckPoint::WriteTowns(const Atlas& atlas)
{
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		hid_t temp = H5Gcreate2(m_file, "Config", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Gclose(temp);
	}

	std::vector<h_town> data;
	for (auto& i : atlas.town_map) {
		h_town toAdd;
		toAdd.latitude = i.first.latitude;
		toAdd.longitude = i.first.longitude;
		toAdd.size = i.second.size;
		toAdd.id = i.second.id;
		toAdd.name = i.second.name.c_str();
		data.push_back(toAdd);
	}
	hid_t strType = H5Tcopy(H5T_C_S1);
	H5Tset_size(strType, H5T_VARIABLE);

	hid_t newType = H5Tcreate(H5T_COMPOUND, sizeof(h_town));

	H5Tinsert(newType, "Latitude", HOFFSET(h_town, latitude), H5T_NATIVE_DOUBLE);
	H5Tinsert(newType, "Longitude", HOFFSET(h_town, longitude), H5T_NATIVE_DOUBLE);
	H5Tinsert(newType, "Size", HOFFSET(h_town, size), H5T_NATIVE_UINT);
	H5Tinsert(newType, "ID", HOFFSET(h_town, id), H5T_NATIVE_UINT);
	H5Tinsert(newType, "Name", HOFFSET(h_town, name), strType);

	hsize_t dims = data.size();
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);

	hid_t dataset = H5Dcreate2(m_file, "Config/Towns", newType, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dataset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Tclose(newType);
	H5Tclose(strType);
	H5Sclose(dataspace);
	H5Dclose(dataset);
}

void CheckPoint::LoadAtlas(Population& pop)
{
	std::string dsetName = "Config/Atlas";
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		return;
	}
	hid_t dset = H5Dopen2(m_file, dsetName.c_str(), H5P_DEFAULT);

	hid_t dspace = H5Dget_space(dset);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	hid_t newType = H5Dget_type(dset);

	std::vector<h_clusterAtlas> data(dims);

	H5Dread(dset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Sclose(dspace);
	H5Dclose(dset);
	H5Tclose(newType);

	for (auto& c : data) {
		Atlas::ClusterKey key(c.ClusterID, (ClusterType)c.ClusterType);
		geo::GeoPosition postion;
		postion.latitude = c.latitude;
		postion.longitude = c.longitude;
		pop.AtlasEmplaceCluster(key, postion);
	}
	LoadTowns(pop);
}

void CheckPoint::LoadTowns(Population& pop)
{
	std::string dsetName = "Config/Towns";
	htri_t exist = H5Lexists(m_file, dsetName.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		return;
	}
	hid_t dset = H5Dopen2(m_file, dsetName.c_str(), H5P_DEFAULT);

	hid_t dspace = H5Dget_space(dset);

	hsize_t dims;
	H5Sget_simple_extent_dims(dspace, &dims, nullptr);

	hid_t newType = H5Dget_type(dset);

	std::vector<h_town> data(dims);

	H5Dread(dset, newType, H5S_ALL, H5S_ALL, H5P_DEFAULT, data.data());

	H5Sclose(dspace);
	H5Dclose(dset);
	H5Tclose(newType);
	Atlas::TownMap map;
	for (auto& i : data) {
		geo::GeoPosition position;
		Atlas::Town town = Atlas::Town("", 0);
		position.latitude = i.latitude;
		position.longitude = i.longitude;

		town.id = i.id;
		town.size = i.size;
		town.name = std::string(i.name);

		map.emplace(position, town);
	}

	pop.AtlasRegisterTowns(map);
}

boost::gregorian::date CheckPoint::GetLastDate()
{
	boost::gregorian::date result;

	auto op_func = [&result](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		if (std::string(name) == "Config") {
			return 0;
		}
		boost::gregorian::date toCheck(boost::gregorian::from_undelimited_string(name));
		if (toCheck > result or result.is_not_a_date()) {
			result = toCheck;
		}
		return 0;
	};
	auto temp = [](hid_t loc_id, const char* name, const H5L_info_t* info, void* operator_data) {
		(*static_cast<decltype(op_func)*>(operator_data))(loc_id, name, info, operator_data);
		return 0;
	};

	H5Literate(m_file, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr, temp, &op_func);
	return result;
}

void CheckPoint::LoadDisease(const std::string& filename) { LoadFile(filename, "Config/disease"); }

void CheckPoint::StoreDisease(const std::string& filename)
{
	// Make the disease to see if it is valid
	try {

		boost::property_tree::ptree pt_disease;
		util::InstallDirs::ReadXmlFile(filename, util::InstallDirs::GetCurrentDir(), pt_disease);
		SingleSimulationConfig temp;
		temp.common_config = std::make_shared<CommonSimulationConfig>();
		temp.common_config->r0 = 0;
		DiseaseProfile disease;
		disease.Initialize(temp, pt_disease);

	} catch (std::exception& e) {
		std::cout << e.what() << std::endl;
		FATAL_ERROR("INVALID DISEASE FILE");
	}
	StoreFile(filename, "disease");
}

void CheckPoint::LoadMatrix(const std::string& filename) { LoadFile(filename, "Config/contact"); }

void CheckPoint::StoreMatrix(const std::string& filename)
{
	// Make the matrix to see if it is valid
	try {
		boost::property_tree::ptree pt_contact;
		util::InstallDirs::ReadXmlFile(filename, util::InstallDirs::GetCurrentDir(), pt_contact);
		ContactProfile(ClusterType::Household, pt_contact);
		ContactProfile(ClusterType::School, pt_contact);
		ContactProfile(ClusterType::Work, pt_contact);
		ContactProfile(ClusterType::PrimaryCommunity, pt_contact);
		ContactProfile(ClusterType::SecondaryCommunity, pt_contact);
	} catch (std::exception& e) {
		FATAL_ERROR("INVALID CONTACT MATRIX");
	}
	StoreFile(filename, "contact");
}

void CheckPoint::LoadConfig(const std::string& filename)
{
	boost::property_tree::ptree pt_config;

	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);

	hid_t attr = H5Aopen(group, "bools", H5P_DEFAULT);
	hbool_t bools[3];
	H5Aread(attr, H5T_NATIVE_HBOOL, bools);
	H5Aclose(attr);

	pt_config.put("TrackIndexCase", bools[0]);

	attr = H5Aopen(group, "doubles", H5P_DEFAULT);
	double doubles[3];
	H5Aread(attr, H5T_NATIVE_DOUBLE, doubles);
	H5Aclose(attr);

	pt_config.put("R0", doubles[0]);

	attr = H5Aopen(group, "uints", H5P_DEFAULT);
	unsigned int uints[6];
	H5Aread(attr, H5T_NATIVE_UINT, uints);
	H5Aclose(attr);

	pt_config.put("RngSeed", uints[0]);
	pt_config.put("Days", uints[1]);
	pt_config.put("LogMode", uints[3]);

	attr = H5Aopen(group, "prefix", H5P_DEFAULT);

	std::unique_ptr<H5A_info_t> info = std::make_unique<H5A_info_t>();

	H5Aget_info(attr, info.get());
	std::vector<char> prefix(info->data_size, '\0');
	H5Aread(attr, H5T_NATIVE_CHAR, prefix.data());
	H5Aclose(attr);

	if (info->data_size == 0) {
		pt_config.put("Prefix", "");
	} else 
	{
		pt_config.put("Prefix",std::string(prefix.begin(), prefix.end()));
	}

	std::ofstream f(filename);
	boost::property_tree::write_json(f, pt_config);
}

void CheckPoint::StoreConfig(const std::string& filename) {
	htri_t exist = H5Lexists(m_file, "Config", H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}

	boost::property_tree::ptree pt_config;
	boost::property_tree::read_json(filename, pt_config);

	bool indexCase = pt_config.get<bool>("TrackIndexCase");
	double r0 = pt_config.get<double>("R0");
	unsigned int seed = pt_config.get<unsigned int>("RngSeed");
	unsigned int days = pt_config.get<unsigned int>("Days");
	unsigned int LogMode = pt_config.get<unsigned int>("LogMode");
	std::string prefix = pt_config.get<std::string>("Prefix");

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);

	hid_t attr = H5Aopen(group, "bools", H5P_DEFAULT);
	hbool_t bools[3];
	H5Aread(attr, H5T_NATIVE_HBOOL, bools);
	H5Aclose(attr);

	bools[0] = indexCase;

	attr = H5Aopen(group, "doubles", H5P_DEFAULT);
	double doubles[3];
	H5Aread(attr, H5T_NATIVE_DOUBLE, doubles);
	H5Aclose(attr);

	doubles[0] = r0;

	attr = H5Aopen(group, "uints", H5P_DEFAULT);
	unsigned int uints[6];
	H5Aread(attr, H5T_NATIVE_UINT, uints);
	H5Aclose(attr);

	uints[0] = seed;
	uints[1] = days;
	uints[3] = LogMode;

	H5Adelete(group,"bools");
	H5Adelete(group,"uints");
	H5Adelete(group,"doubles");
	H5Adelete(group,"prefix");

	hsize_t dims = 3;
	hid_t dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "bools", H5T_NATIVE_HBOOL, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_HBOOL, bools);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = 6;
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "uints", H5T_NATIVE_UINT, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_UINT, uints);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = 3;
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "doubles", H5T_NATIVE_DOUBLE, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_DOUBLE, doubles);
	H5Sclose(dataspace);
	H5Aclose(attr);

	dims = prefix.size();
	dataspace = H5Screate_simple(1, &dims, nullptr);
	attr = H5Acreate2(group, "prefix", H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_CHAR, prefix.c_str());
	H5Sclose(dataspace);
	H5Aclose(attr);
}

void CheckPoint::LoadFile(const std::string& filestr, const std::string& setname)
{
	htri_t exist = H5Lexists(m_file, setname.c_str(), H5P_DEFAULT);
	if (exist <= 0) {
		FATAL_ERROR("Invalid file");
	}
	boost::filesystem::path filename(filestr);
	/*
	Piece of code based on code on
	https://lists.hdfgroup.org/pipermail/hdf-forum_lists.hdfgroup.org/2010-June/003208.html
	*/
	std::vector<char> data;
	hid_t did;
	herr_t err = 0;
	hid_t spaceId;
	hid_t dataType = H5T_NATIVE_CHAR;

	// std::cout << "HDF5 Data Type: " <<	H5Lite::HDFTypeForPrimitiveAsStr(test) << std::endl;
	/* Open the dataset. */
	// std::cout << "  Opening " << dsetName << " for data Retrieval.  "<< std::endl;
	did = H5Dopen(m_file, setname.c_str(), H5P_DEFAULT);

	if (did < 0) {
		std::cout << " Error opening Dataset: " << did << std::endl;
		return;
	}

	std::string extension;
	hid_t attr = H5Aopen(did, "extension", H5P_DEFAULT);

	std::unique_ptr<H5A_info_t> info = std::make_unique<H5A_info_t>();

	H5Aget_info(attr, info.get());
	std::vector<char> extensionVector(info->data_size, '\0');
	H5Aread(attr, H5T_NATIVE_CHAR, extensionVector.data());
	H5Aclose(attr);

	extension = std::string(extensionVector.begin(), extensionVector.end());

	filename.replace_extension(extension);

	std::ofstream out(filename.string(), std::ios::out | std::ios::trunc);
	if (did >= 0) {
		spaceId = H5Dget_space(did);
		if (spaceId > 0) {
			int32_t rank = H5Sget_simple_extent_ndims(spaceId);
			if (rank > 0) {
				std::vector<hsize_t> dims;
				dims.resize(rank); // Allocate enough room for the dims
				rank = H5Sget_simple_extent_dims(spaceId, &(dims.front()), NULL);
				hsize_t numElements = 1;
				for (std::vector<hsize_t>::iterator iter = dims.begin(); iter < dims.end(); ++iter) {
					numElements = numElements * (*iter);
				}
				// std::cout << "NumElements: " << numElements << std::endl;
				// Resize the vector
				data.resize(static_cast<int>(numElements));
				// for (uint32_t i = 0; i<numElements; ++i) { data[i] = 55555555;}
				err = H5Dread(did, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, &(data.front()));
				if (err < 0) {
					std::cout << "Error Reading Data." << std::endl;
				}
			}
			err = H5Sclose(spaceId);
			if (err < 0) {
				std::cout << "Error Closing Data Space" << std::endl;
			}
		} else {
			std::cout << "Error Opening SpaceID" << std::endl;
		}
		err = H5Dclose(did);
		if (err < 0) {
			std::cout << "Error Closing Dataset" << std::endl;
		}
		for (auto& c : data) {
			out << c;
		}
	}
	return;
}

void CheckPoint::StoreFile(const std::string& filename, const std::string& setname)
{
	boost::filesystem::path filep(filename);
	if (!is_regular_file(filep)) {
		FATAL_ERROR("Unable to find file: " + filep.string());
	}
	std::string extension = filep.extension().string();
	std::ifstream f(filep.string());

	std::vector<char> dataset{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};

	hid_t group = H5Gopen2(m_file, "Config", H5P_DEFAULT);
	hid_t dset;
	if (H5Lexists(group, setname.c_str(), H5P_DEFAULT) <= 0) {
		hsize_t dims[1] = {dataset.size()};
		hid_t dataspace = H5Screate_simple(1, dims, nullptr);
		dset = H5Dcreate2(
		    group, setname.c_str(), H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		H5Sclose(dataspace);
	} else {
		dset = H5Dopen2(group, setname.c_str(), H5P_DEFAULT);
	}
	H5Dwrite(dset, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, dataset.data());
	if (H5Aexists(dset, "extension") > 0) {
		H5Adelete(dset, "extension");
	}
	hsize_t attrDims = extension.size();
	hid_t dataspace = H5Screate_simple(1, &attrDims, nullptr);
	hid_t attr = H5Acreate2(dset, "extension", H5T_NATIVE_CHAR, dataspace, H5P_DEFAULT, H5P_DEFAULT);
	H5Awrite(attr, H5T_NATIVE_CHAR, extension.c_str());
	H5Aclose(attr);

	H5Dclose(dset);
	H5Sclose(dataspace);
	H5Gclose(group);
}
} /* namespace checkpoint */
} /* namespace stride */
