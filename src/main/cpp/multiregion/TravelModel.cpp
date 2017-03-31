#include "TravelModel.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace stride {
namespace multiregion {

RegionTravel::RegionTravel(RegionId region_id, const std::vector<std::shared_ptr<const Airport>>& all_airports)
    : region_id(region_id), all_airports(all_airports)
{
    for (const auto& airport : all_airports) {
        if (airport->region_id == region_id) {
            local_airports.push_back(airport);
        } else {
            for (const auto& route : airport->routes) {
                if (route.target->region_id == region_id)
                    regions_with_incoming_routes.insert(airport->region_id);
            }
        }
    }
}

}
}