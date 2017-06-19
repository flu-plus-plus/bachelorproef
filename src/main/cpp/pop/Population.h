#ifndef POPULATION_H_INCLUDED
#define POPULATION_H_INCLUDED

#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <vector>
#include "Person.h"
#include "core/Atlas.h"
#include "core/Health.h"
#include "geo/GeoPosition.h"
#include "util/Parallel.h"
#include "util/ParallelMap.h"
#include "util/Random.h"

namespace stride {

/**
 * Stores Persons, along with an Atlas mapping their clusters to GeoPositions.
 */
class Population
{
private:
	util::parallel::ParallelMap<PersonId, std::shared_ptr<PersonData>> people;
	PersonId max_person_id;
	Atlas atlas;
	bool has_atlas_flag;

public:
	/// Creates a population. No atlas is associated with the population.
	Population() : has_atlas_flag(false) {}

	/// Creates a population. The given Boolean specifies if the population
	/// includes an atlas.
	Population(bool has_atlas) : has_atlas_flag(has_atlas) {}

	Population(const Population&) = delete;
	Population& operator=(const Population&) = delete;

	Population(Population&&) = default;
	Population& operator=(Population&&) = default;

	/// An iterator implementation for Population containers.
	class const_iterator final
	{
	public:
		typedef decltype(people)::const_iterator map_iterator;

		const_iterator(const map_iterator& map_iterator_val) : map_iterator_val(map_iterator_val) {}
		const_iterator(const const_iterator&) = default;
		const_iterator& operator++()
		{
			++map_iterator_val;
			return *this;
		}
		const_iterator operator++(int)
		{
			auto result = *this;
			map_iterator_val++;
			return result;
		}
		const_iterator& operator--()
		{
			--map_iterator_val;
			return *this;
		}
		const_iterator operator--(int)
		{
			auto result = *this;
			map_iterator_val--;
			return result;
		}
		Person operator*() const { return Person(map_iterator_val->first, map_iterator_val->second); }
		bool operator==(const const_iterator& other) const
		{
			return map_iterator_val == other.map_iterator_val;
		}
		bool operator!=(const const_iterator& other) const
		{
			return map_iterator_val != other.map_iterator_val;
		}

		friend void swap(const_iterator& lhs, const_iterator& rhs);

	private:
		map_iterator map_iterator_val;
	};

	typedef const_iterator iterator;

	/// Inserts a new element into the container constructed in-place with the given args.
	template <typename... TArgs>
	const_iterator emplace(TArgs&&... args)
	{
		Person value(args...);
		if (value.GetId() > max_person_id)
			max_person_id = value.GetId();

		return const_iterator(people.emplace(value.GetId(), value.GetData()).first);
	}

	/// Extracts the person with the given id from this population.
	Person extract(PersonId id)
	{
		Person result(id, people.find(id)->second);
		people.erase(id);
		return result;
	}

	/// Gets the person with the given id in this population.
	Person getPerson(PersonId id) const
	{
		Person result(id, people.find(id)->second);
		return result;
	}

	/// Gets the number of people in this population.
	auto size() const -> decltype(people.size()) { return people.size(); }

	/// Tests if this population uses an atlas.
	bool has_atlas() const { return has_atlas_flag; }

	/// Sets the has_atlas_flag.
	void set_has_atlas(bool has_atlas) { has_atlas_flag = has_atlas; }

	/// Gets this population's atlas.
	const Atlas& get_atlas() const { return atlas; }

	/// Creates a constant iterator positioned at the first person in this population.
	const_iterator begin() const { return const_iterator(people.begin()); }

	/// Creates a constant iterator positioned just past the last person in this population.
	const_iterator end() const { return const_iterator(people.end()); }

	/// Gets the largest id for any person that has ever been in this population.
	PersonId get_max_id() const { return max_person_id; }

	/// Gets a list of pointers to 'count' unique, randomly chosen participants in the population.
	std::vector<Person> get_random_persons(util::Random& rng, std::size_t count);

	/// Gets a list of pointers to 'count' unique, randomly chosen participants in the population
	/// which satisfy the given predicate.
	std::vector<Person> get_random_persons(
	    util::Random& rng, std::size_t count, std::function<bool(const Person&)> matches);

	/// Get the cumulative number of cases.
	unsigned int get_infected_count() const;

	/// Get the fraction of the population that is infected.
	double get_fraction_infected() const { return double(get_infected_count()) / size(); }

	template <typename BeliefPolicy>
	unsigned int get_adopted_count() const
	{
		unsigned int total{0U};
		for (const auto& p : *this) {
			auto belief_data = p.GetBeliefData();
			bool adopted = BeliefPolicy::HasAdopted(belief_data);
			if (adopted) {
				total++;
			}
		}
		return total;
	}

	/// Register the map of GeoPositions to Towns to the atlas.
	void atlas_register_towns(const Atlas::TownMap& towns) { atlas.RegisterTowns(towns); }

	/// Store a Cluster's GeoPosition in the population's atlas.
	auto atlas_emplace_cluster(const Atlas::ClusterKey& key, const geo::GeoPosition& pos)
	    -> decltype(atlas.EmplaceCluster(key, pos))
	{
		return atlas.EmplaceCluster(key, pos);
	}

	/// Gets the given person's hometown.
	const Atlas::Town& get_hometown(const Person& person) const
	{
		return atlas.LookupTown({person.GetClusterId(ClusterType::Household), ClusterType::Household});
	}

	/// Runs the `action` on every element of this vector. Up to `number_of_threads` instances of
	/// the `action` are run at the same time. `action` must be invocable with signature
	/// `void(const Person& person, unsigned int thread_number)`.
	template <typename TAction>
	void parallel_for(unsigned int number_of_threads, const TAction& action) const
	{
		stride::util::parallel::parallel_for(
		    people, number_of_threads,
		    [&action](const PersonId& id, const std::shared_ptr<PersonData>& data, unsigned int thread_number) {
			    return action(Person(id, data), thread_number);
		    });
	}

	/// Runs the `action` on every element of this vector. `action` must be invocable with signature
	/// `void(const Person& person, unsigned int dummy)`.
	template <typename TAction>
	void serial_for(const TAction& action) const
	{
		stride::util::parallel::serial_for(
		    people,
		    [&action](const PersonId& id, const std::shared_ptr<PersonData>& data, unsigned int thread_number) {
			    return action(Person(id, data), thread_number);
		    });
	}
};

/// Swaps two population iterators.
inline void swap(typename Population::const_iterator& lhs, typename Population::const_iterator& rhs)
{
	std::swap(lhs.map_iterator_val, rhs.map_iterator_val);
}

using PopulationRef = std::shared_ptr<const Population>;

} // end_of_namespace

namespace std {
template <>
struct iterator_traits<stride::Population::const_iterator>
{
	typedef const stride::Person value_type;
	typedef value_type& reference;
	typedef value_type* pointer;
};
}

#endif // end of include guard
