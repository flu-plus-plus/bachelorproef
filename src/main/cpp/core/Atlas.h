#ifndef ATLAS_H_INCLUDED
#define ATLAS_H_INCLUDED

/**
 * @file
 * Header for the Atlas class.
 */

#include <cstddef>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include "core/ClusterType.h"
#include "geo/GeoPosition.h"

namespace stride {
/**
 * A map storing geopositions for clusters, and town data for geopositions.
 */
class Atlas
{
public:
	/// A struct storing information on a town/city
	struct Town
	{
		// The name of the town or city
		std::string name;
		// The number of inhabitants
		unsigned int size;
		// The unique ID
		std::size_t id;

		Town(const std::string& name, const unsigned int size) : name(name), size(size), id(newId++) {}

	private:
		// Static member used to produce unique IDs
		static std::size_t newId;
	};

	using ClusterKey = std::pair<std::size_t, ClusterType>;
	using ClusterMap = std::map<ClusterKey, geo::GeoPosition>;
	using TownMap = std::map<geo::GeoPosition, Town>;

	Atlas() {}
	Atlas(const ClusterMap& cluster_map) : cluster_map(cluster_map) {}

	/// Look up a cluster's position. If it isn't found, throw std::out_of_range.
	const geo::GeoPosition& LookupPosition(const ClusterKey& key) const { return cluster_map.at(key); }

	/// Look up a cluster's town. If it isn't found, throw std::out_of_range.
	const Town& LookupTown(const ClusterKey& key) const { return town_map.at(cluster_map.at(key)); }

	/// Get the map of towns.
	const TownMap& getTownMap() const { return town_map; }

	/// Store a cluster's GeoPosition in the atlas.
	auto EmplaceCluster(const Atlas::ClusterKey& key, const geo::GeoPosition& pos)
	    -> decltype(ClusterMap().emplace(key, pos))
	{
		return cluster_map.emplace(key, pos);
	}

	/// Associate a GeoPosition with a specific Town.
	void RegisterTowns(const TownMap& towns) { town_map = towns; }

	ClusterMap cluster_map;
	TownMap town_map;
};

} // end_of_namespace

#endif // include-guard
