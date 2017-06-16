#ifndef VISUALIZERDATA_H_INCLUDED
#define VISUALIZERDATA_H_INCLUDED

/**
 * @file
 * Data structure for the visualiser.
 */

#include <memory>
#include <string>
#include <vector>
#include <boost/property_tree/ptree.hpp>
#include "pop/Population.h"

namespace stride {

/**
 * Data structure for the visualiser.
 * Stores the day-by-day infected count per Town.
 */
class VisualizerData
{
private:
	/// Vector of days: Each day is a map from Town names to the amount of infected.
	std::vector<std::map<std::size_t, int>> days;
	std::shared_ptr<boost::property_tree::ptree> daysTree;
	std::shared_ptr<boost::property_tree::ptree> townsTree;

public:
	VisualizerData() : daysTree(new boost::property_tree::ptree()) {}

	/// Register a new day from the given population's current state.
	void AddDay(const std::shared_ptr<const Population>& pop);

	/// Get a reference to the vector of days.
	const std::vector<std::map<std::size_t, int>>& GetDays() const;

	/// Get the day-by-day data as a boost property tree.
	std::shared_ptr<boost::property_tree::ptree> GetDaysTree();

	/// Get the town data as a boost property tree.
	std::shared_ptr<boost::property_tree::ptree> GetTownsTree() { return townsTree; };

private:
	/// Convert and store the town map as a ptree.
	void RegisterTowns(const Atlas::TownMap& townMap);
};

} // end of namespace

#endif // end-of-include-guard