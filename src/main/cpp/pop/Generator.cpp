#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <numeric>
#include <vector>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <spdlog/spdlog.h>

#include "Generator.h"
#include "Population.h"

#include "alias/Alias.h"
#include "core/Cluster.h"
#include "core/ClusterType.h"
#include "sim/SimulationConfig.h"
#include "util/Errors.h"
#include "util/InstallDirs.h"
#include "util/Parallel.h"

namespace stride {
namespace population {

std::unique_ptr<Generator> Generator::FromConfig(
    const SingleSimulationConfig& config, const disease::Disease& disease, util::Random& rng)
{
	// Read geodistribution profile.
	auto geo_file = util::InstallDirs::OpenDataFile(config.GetGeodistributionProfilePath());
	const geo::ProfileRef geo_profile = geo::Profile::Parse(*geo_file);

	// Read reference households.
	auto households_file = util::InstallDirs::OpenDataFile(config.GetReferenceHouseholdsPath());
	boost::property_tree::ptree hpt;
	boost::property_tree::read_json(*households_file, hpt);
	const auto reference_households = population::ParseReferenceHouseholds(hpt);

	// Read population model file.
	auto pop_file = util::InstallDirs::OpenDataFile(config.GetPopulationPath());
	boost::property_tree::ptree pt;
	boost::property_tree::read_xml(*pop_file, pt);
	const population::ModelRef model = population::Model::Parse(pt);

	return std::make_unique<Generator>(model, geo_profile, reference_households, disease, rng);
}

/// Calculate the sum of all the values of a map.
template <typename K, typename V>
V SumValues(const std::map<K, V>& map)
{
	V sum = 0;
	for (const auto& p : map) {
		sum += p.second;
	}
	return sum;
}

Population Generator::Generate()
{
	// Types used in this method.
	using stride::geo::GeoPosition;
	using GeoBRNG = alias::BiasedRandomValueGenerator<GeoPosition>;
	using SchoolClusterId = ClusterId;
	using WorkClusterId = ClusterId;
	using CommunityClusterId = ClusterId;
	using HouseholdClusterId = ClusterId;
	using School = std::vector<SchoolClusterId>;
	using College = School;

	// The population object we're filling up.
	Population population(true);

	// Throughout this method, `n` is a counter for how many people are
	// left to accomodate for. When towns, schools, etc. are added to the
	// simulation area, the number of people they can serve is subtracted
	// from `n`, and facilities keep being added until `n <= 0`.
	int n;

	// Generated cluster data.
	std::map<GeoPosition, std::vector<ReferenceHousehold>> households;
	std::map<GeoPosition, std::vector<School>> schools;
	std::map<GeoPosition, College> colleges;
	std::map<GeoPosition, std::vector<WorkClusterId>> workplaces;
	std::map<GeoPosition, std::vector<CommunityClusterId>> primary_communities;
	std::map<GeoPosition, std::vector<CommunityClusterId>> secondary_communities;

	// A map of locations to Towns (or cities)
	std::map<GeoPosition, Atlas::Town> town_map;

	// Generate cities.
	{
		Debug("Generating cities...");
		int cities_created = 0;
		n = model->population_size;
		for (const auto& city : geo_profile->GetCities()) {
			int city_size = city.relative_population * model->city_ratio * model->population_size;
			n -= city_size;
			bool inserted = town_map.insert({city.geo_position, Atlas::Town(city.name, city_size)}).second;
			if (!inserted) {
				FATAL_ERROR("Overlapping coordinates in geoprofile city list!");
			}
			cities_created++;
		}
		Debug("Created {} cities.", cities_created);
	}

	// Generate towns.
	{
		Debug("Generating towns...");
		int towns_created = 0;
		while (n > 0) {
			int town_size = GetRandomTownSize();
			auto pos = GetRandomGeoPosition();
			std::string town_name = "town" + std::to_string(towns_created);
			bool inserted = town_map.insert({pos, Atlas::Town(town_name, town_size)}).second;
			if (inserted) {
				n -= town_size;
			}
			towns_created++;
		}
		Debug("Created {} towns.", towns_created);
	}

	// Store all town/city locations in the population's atlas.
	population.atlas_register_towns(town_map);

	// A map from GeoPositions to # of people to generate there: A dressed-down town_map.
	std::map<GeoPosition, int> population_distribution;
	for (const auto& p : town_map) {
		population_distribution.insert({p.first, p.second.size});
	}

	// A GeoPosition distribution, biased towards locations with high populations.
	auto geo_brng = GeoBRNG::CreateDistribution(population_distribution, random);

	// Generate households; find school/work distribution.
	int num_college_students = 0;
	std::map<GeoPosition, int> school_population_distribution;
	std::map<GeoPosition, int> work_population_distribution;
	{
		Debug("Generating households...");
		int households_created = 0;
		n = model->population_size;
		while (n > 0) {
			const auto geo_position = geo_brng.Next();
			const auto rh = GetRandomReferenceHousehold();
			households[geo_position].emplace_back(rh);

			for (int age : rh.ages) {
				if (model->IsSchoolAge(age))
					school_population_distribution[geo_position]++;
				if (model->IsEmployableAge(age))
					work_population_distribution[geo_position]++;
				if (model->IsCollegeAge(age))
					num_college_students++;
			}

			n -= rh.ages.size();
			households_created++;
		}
		Debug("Created {} households.", households_created);
	}

	// Generate schools.
	std::map<GeoPosition, double> college_distribution;
	{
		Debug("Generating schools...");
		SchoolClusterId school_cluster_id = 1;
		int schools_created = 0;

		auto school_geo_brng = GeoBRNG::CreateDistribution(school_population_distribution, random);
		const int clusters_per_school = model->school_size / model->school_cluster_size;

		n = SumValues(school_population_distribution);
		while (n > 0) {
			auto& vec = schools[school_geo_brng.Next()];
			vec.emplace_back(School());
			for (int i = 0; i < clusters_per_school; i++)
				vec.back().emplace_back(school_cluster_id++);
			n -= model->school_size;
			schools_created++;
		}
		Debug("Created {} schools.", schools_created);

		// Generate colleges.
		Debug("Generating colleges...");
		const int clusters_per_college = model->college_size / model->college_cluster_size;
		int colleges_created = 0;

		// Loop over cities in descending order of population.
		n = num_college_students;
		for (const auto& city : geo_profile->GetCities()) {
			if (n <= 0)
				break;

			College& college = colleges[city.geo_position];
			for (int i = 0; i < clusters_per_college; i++)
				college.emplace_back(school_cluster_id++);

			college_distribution[city.geo_position] = city.relative_population;

			n -= model->college_size;
			colleges_created++;
		}
		Debug("Created {} colleges.", colleges_created);
	}

	// This biased distribution is sampled from for students who commute to college.
	auto college_brng = GeoBRNG::CreateDistribution(college_distribution, random);

	// This biased distribution is sampled from for employees who commute to work.
	auto work_geo_brng = GeoBRNG::CreateDistribution(work_population_distribution, random);

	// Generate workplaces.
	{
		Debug("Generating workplaces...");
		WorkClusterId work_cluster_id = 1;
		int workplaces_created = 0;

		n = SumValues(work_population_distribution);
		while (n > 0) {
			workplaces[work_geo_brng.Next()].emplace_back(work_cluster_id++);
			n -= model->workplace_size;
			workplaces_created++;
		}
		Debug("Created {} workplaces.", workplaces_created);
	}

	// Generate communities.
	{
		Debug("Generating primary communities...");
		CommunityClusterId primary_community_cluster_id = 1;
		int primary_communities_created = 0;

		n = SumValues(population_distribution);
		while (n > 0) {
			primary_communities[geo_brng.Next()].emplace_back(primary_community_cluster_id++);
			n -= model->community_size;
			primary_communities_created++;
		}

		Debug("Created {} primary communities.", primary_communities_created);
	}

	// Generate secondary communities.
	{
		Debug("Generating secondary communities...");
		CommunityClusterId secondary_community_cluster_id = 1;
		int secondary_communities_created = 0;

		n = SumValues(population_distribution);
		while (n > 0) {
			secondary_communities[geo_brng.Next()].emplace_back(secondary_community_cluster_id++);
			n -= model->community_size;
			secondary_communities_created++;
		}

		Debug("Created {} secondary communities.", secondary_communities_created);
	}

	// Generate people.
	{
		Debug("Generating people...");
		HouseholdClusterId household_id = 1;
		ClusterId person_id = 1;

		for (const auto& p : households) {
			const auto& home = p.first;
			for (const auto& rh : p.second) {
				for (const int age : rh.ages) {
					int school_id = 0;
					int work_id = 0;
					if (model->IsSchoolAge(age)) {
						school_id = random.Sample(random.Sample(FindLocal(home, schools)));
					} else if (model->IsCollegeAge(age)) {
						bool commutes = random.Chance(model->college_commute_ratio);
						school_id = random.Sample(
						    commutes ? colleges[college_brng.Next()]
							     : FindLocal(home, colleges));
					} else if (
					    model->IsEmployableAge(age) && random.Chance(model->employed_ratio)) {
						// TODO: technically, commuters should commute to workplaces in big
						// cities, not just random ones.
						bool commutes = random.Chance(model->work_commute_ratio);
						work_id = random.Sample(
						    FindLocal(commutes ? work_geo_brng.Next() : home, workplaces));
					}

					int primary_community_id = random.Sample(FindLocal(home, primary_communities));
					int secondary_community_id =
					    random.Sample(FindLocal(home, secondary_communities));

					population.emplace(
					    person_id++, age, household_id, school_id, work_id, primary_community_id,
					    secondary_community_id, disease.Sample(random));

					if (person_id % 10000 == 0) {
						Debug("Person {}...", person_id);
					}
				}
				household_id++;
			}
		}
		Debug("Generated {} people.", person_id - 1);
	}

	// Store all the cluster's locations in the population's atlas.
	{
		Debug("Creating atlas of cluster locations...");
		HouseholdClusterId household_id = 1;
		for (const auto& p : households)
			for (auto it = p.second.begin(); it != p.second.end(); ++it)
				population.atlas_emplace_cluster({household_id++, ClusterType::Household}, p.first);

		for (const auto& p : schools)
			for (const auto& school : p.second)
				for (SchoolClusterId id : school)
					population.atlas_emplace_cluster({id, ClusterType::School}, p.first);

		for (const auto& p : colleges)
			for (SchoolClusterId id : p.second)
				population.atlas_emplace_cluster({id, ClusterType::School}, p.first);

		for (const auto& p : workplaces)
			for (WorkClusterId id : p.second)
				population.atlas_emplace_cluster({id, ClusterType::Work}, p.first);

		for (const auto& p : primary_communities)
			for (CommunityClusterId id : p.second)
				population.atlas_emplace_cluster({id, ClusterType::PrimaryCommunity}, p.first);

		for (const auto& p : secondary_communities)
			for (CommunityClusterId id : p.second)
				population.atlas_emplace_cluster({id, ClusterType::SecondaryCommunity}, p.first);
	}

	/* {
		Debug("Writing .SVG file of towns and cities...");
		std::ofstream svg_file;
		svg_file.open("towns_and_cities.svg");
		svg_file << "<svg width=\"500\" height=\"400\" viewBox=\"2 49 5 3\"
	xmlns=\"http://www.w3.org/2000/svg\">\n";
		for (const auto& p : population_distribution) {
			Debug("{} {} {}", p.first.longitude, p.first.longitude, p.second);
			svg_file << "  <circle cx=\"" << p.first.longitude
				<< "\" cy=\"" << (101.0 - p.first.latitude)
				<< "\" r=\"" << (std::log(p.second) * 0.005) << "\"/>\n";
		}
		svg_file << "</svg>\n";
		svg_file.close();
	} */

	Debug("Done!");
	return population;
}

bool Generator::FitsModel(const Population& population)
{
	Debug("Checking if population fits model...");

	// Check the population size.
	const double population_ratio =
	    static_cast<double>(population.size()) / static_cast<double>(model->population_size);
	if (population_ratio < 0.9 || population_ratio > 1.1) {
		Debug("Population size deviates more than 10% from goal");
		return false;
	}

	// Check the generated persons' ages.
	std::atomic<bool> all_ages_valid(true);
	population.parallel_for(
	    util::parallel::get_number_of_threads(), [this, &all_ages_valid](const Person& person, unsigned int) {
		    const int age = person.GetAge();
		    const bool is_student_age = model->IsSchoolAge(age) || model->IsCollegeAge(age);
		    if (person.GetClusterId(ClusterType::School) && !is_student_age) {
			    Debug("Generated student with invalid age: {}", age);
			    all_ages_valid = false;
		    }
		    if (person.GetClusterId(ClusterType::Work) && !model->IsEmployableAge(age)) {
			    Debug("Generated employee with invalid age: {}", age);
			    all_ages_valid = false;
		    }
	    });

	// You could write a billion checks like this, of course. Is it worth it?

	return all_ages_valid;
}

} // namespace population
} // namespace stride
