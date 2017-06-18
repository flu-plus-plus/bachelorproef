#ifndef UTIL_PARALLEL_MAP_H_INCLUDED
#define UTIL_PARALLEL_MAP_H_INCLUDED

#include <map>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>
#include "Parallel.h"

namespace stride {
namespace util {
namespace parallel {

/// An ordered map that supports parallel iteration. This type exposes the same
/// interface as std::map and extends it with parallelization methods.
template <typename... TArgs>
class ParallelMap final
{
	// Some implementation details:
	// ===========================
	//
	// (If you don't care about the implementation please skip to the bullets at the
	//  end of this paragraph. You should probably read them if you're considering to
	//  use `ParallelMap`.)
	//
	// `ParallelMap`'s main attraction is `parallel_for`, which allows us to parallelize
	// actions on (dense) maps. However, the standard library makes it hard to implement
	// this functionality on top of `std::map`.
	//
	// At the core of `ParallelMap::parallel_for` is the "insert hack," which inserts
	// dummy elements (and later deletes them!) to carve the map into chunks that can
	// be distributed across different threads. Note that this actually mutates the map,
	// so `parallel_for` is, at first glance, not thread-safe, even if we don't touch
	// the map's contents. That's super counter-intuitive and will probably lead to
	// hordes of bugs.
	//
	// ParallelMap makes `parallel_for` thread-safe again by maintaining a reader/writer
	// lock. Wrappers for `std::map` operations acquire a reader lock and `parallel_for`
	// acquires a writer lock while it's mutating the map. This makes it seem like
	// `parallel_for` doesn't mutate the map's state at all, because `parallel_for`
	// restores the map's state after it's cut the map up into chunks and no more than
	// one writer lock can be active at the same time.
	//
	// Conversely, any number of reader locks can be active at the same time as long as
	// no writer locks are active. So `ParallelMap` will not make any threads wait for
	// a lock as long as `parallel_for` is never called.
	//
	// Here's a quick summary of what this design means for `ParallelMap` users:
	//
	//     1. Iterators and operations that were inherited from `std::map` are slower
	//        because they need to acquire a lock. Tthey probably won't have to wait
	//        for the lock, but I guess merely asking for a lock probably has a measurable
	//        performance overhead.
	//
	//        Try to use `serial_for` or `parallel_for` if at all possible; these methods
	//        are far less granular and hence acquire far fewer locks.
	//
	//     2. `parallel_for` is as thread-safe as the `parallel_for` implementation for
	//        for `std::vector`: if you mutate the container's element, then you might get
	//        data races.
	//
	//        All the locking scheme does is hide the fact that `parallel_for` mutates the map.
	//
	//     3. Non-parallel operations have the same thread-safety as before. The locking
	//        scheme will not (and cannot because of how iterators work!) avoid data races
	//        that arise from using them.

private:
	using inner_map_type = std::map<TArgs...>;
	using this_map_type = ParallelMap<TArgs...>;
	using shared_mutex_type = std::shared_timed_mutex;
	mutable inner_map_type vals;
	mutable shared_mutex_type parallel_iter_mutex;

	/// An iterator wrapper that acquires read locks to maintain the illusion
	/// that parallel_for does not mutate any data.
	template <typename TIterator>
	class IteratorWrapper final : public std::iterator<typename std::iterator_traits<TIterator>::iterator_category,
							   typename std::iterator_traits<TIterator>::value_type,
							   typename std::iterator_traits<TIterator>::difference_type,
							   typename std::iterator_traits<TIterator>::pointer,
							   typename std::iterator_traits<TIterator>::reference>
	{
	private:
		using inner_iterator_type = TIterator;
		using this_iterator_type = IteratorWrapper<inner_iterator_type>;
		inner_iterator_type inner_iterator;
		using value_type = typename std::iterator_traits<TIterator>::value_type;
		using pointer = typename std::iterator_traits<TIterator>::pointer;
		shared_mutex_type* mutex_ptr;

		template <typename>
		friend class IteratorWrapper;

	public:
		IteratorWrapper(const inner_iterator_type& inner_iterator, shared_mutex_type* mutex_ptr)
		    : inner_iterator(inner_iterator), mutex_ptr(mutex_ptr)
		{
		}

		template <typename TOtherIterator>
		IteratorWrapper(const IteratorWrapper<TOtherIterator>& other)
		    : inner_iterator(other.inner_iterator), mutex_ptr(other.mutex_ptr)
		{
		}

		template <typename TOtherIterator>
		IteratorWrapper(IteratorWrapper<TOtherIterator>&& other)
		    : inner_iterator(other.inner_iterator), mutex_ptr(other.mutex_ptr)
		{
		}

		IteratorWrapper(const this_iterator_type&) = default;
		IteratorWrapper(this_iterator_type&&) = default;
		this_iterator_type& operator=(const this_iterator_type& other) = default;
		this_iterator_type& operator=(this_iterator_type&& other) = default;

		value_type& operator*() const
		{
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			return inner_iterator.operator*();
		}

		pointer operator->() const
		{
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			return inner_iterator.operator->();
		}

		this_iterator_type& operator++()
		{
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			++inner_iterator;
			return *this;
		}

		this_iterator_type operator++(int)
		{
			auto old_value = *this;
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			inner_iterator++;
			return old_value;
		}

		this_iterator_type& operator--()
		{
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			--inner_iterator;
			return *this;
		}

		this_iterator_type operator--(int)
		{
			auto old_value = *this;
			std::shared_lock<shared_mutex_type> read_lock{*mutex_ptr};
			inner_iterator--;
			return old_value;
		}

		void swap(this_iterator_type& other) { std::swap(inner_iterator, other.inner_iterator); }

		bool operator==(const this_iterator_type& other) const
		{
			return inner_iterator == other.inner_iterator;
		}

		bool operator!=(const this_iterator_type& other) const
		{
			return inner_iterator != other.inner_iterator;
		}
	};

	/// Wraps the given iterator in a thread-safe wrapper.
	template <typename TIterator>
	IteratorWrapper<TIterator> make_iterator(const TIterator& value) const
	{
		return IteratorWrapper<TIterator>(value, &parallel_iter_mutex);
	}

	/// Wraps the given iterator in a thread-safe wrapper.
	template <typename TIterator>
	IteratorWrapper<TIterator> make_iterator(TIterator&& value) const
	{
		return IteratorWrapper<TIterator>(value, &parallel_iter_mutex);
	}

	/// Wraps the given pair of iterators in a thread-safe wrapper.
	template <typename TIterator>
	std::pair<IteratorWrapper<TIterator>, IteratorWrapper<TIterator>> make_iterator(
	    const std::pair<TIterator, TIterator>& pair) const
	{
		return std::make_pair(make_iterator<TIterator>(pair.first), make_iterator<TIterator>(pair.second));
	}

	/// Wraps the given pair of iterators in a thread-safe wrapper.
	template <typename TIterator>
	std::pair<IteratorWrapper<TIterator>, IteratorWrapper<TIterator>> make_iterator(
	    std::pair<TIterator, TIterator>&& pair) const
	{
		return std::make_pair(
		    std::move(make_iterator<TIterator>(pair.first)), std::move(make_iterator<TIterator>(pair.second)));
	}

	/// Wraps the given pair containing an iterator in a thread-safe wrapper.
	template <typename TIterator>
	std::pair<IteratorWrapper<TIterator>, bool> make_iterator(const std::pair<TIterator, bool>& pair) const
	{
		return std::make_pair(std::move(make_iterator<TIterator>(pair.first)), pair.second);
	}

	/// Wraps the given pair containing an iterator in a thread-safe wrapper.
	template <typename TIterator>
	std::pair<IteratorWrapper<TIterator>, bool> make_iterator(std::pair<TIterator, bool>&& pair) const
	{
		return std::make_pair(std::move(make_iterator<TIterator>(pair.first)), pair.second);
	}

public:
	using key_type = typename inner_map_type::key_type;
	using mapped_type = typename inner_map_type::mapped_type;
	using value_type = typename inner_map_type::value_type;
	using size_type = typename inner_map_type::size_type;
	using difference_type = typename inner_map_type::difference_type;
	using key_compare = typename inner_map_type::key_compare;
	using allocator_type = typename inner_map_type::allocator_type;
	using reference = typename inner_map_type::reference;
	using const_reference = typename inner_map_type::const_reference;
	using pointer = typename inner_map_type::pointer;
	using const_pointer = typename inner_map_type::const_pointer;
	using iterator = IteratorWrapper<typename inner_map_type::iterator>;
	using const_iterator = IteratorWrapper<typename inner_map_type::const_iterator>;
	using reverse_iterator = IteratorWrapper<typename inner_map_type::reverse_iterator>;
	using const_reverse_iterator = IteratorWrapper<typename inner_map_type::const_reverse_iterator>;
	using value_compare = typename inner_map_type::value_compare;

	/// Constructs a concurrent map.
	template <typename... TCtorArgs>
	ParallelMap(TCtorArgs... args) : vals(args...), parallel_iter_mutex()
	{
	}

	ParallelMap(const this_map_type& other) : vals(other.vals), parallel_iter_mutex() {}
	ParallelMap(this_map_type&& other) : vals(std::move(other.vals)), parallel_iter_mutex() {}

	/// Gets a reference to this parallel map's inner map data structure.
	inner_map_type& get_inner_map() { return vals; }

	/// Gets a reference to this parallel map's inner map data structure.
	const inner_map_type& get_inner_map() const { return vals; }

	/// Gets a reference to this parallel map's parallel iteration mutex.
	shared_mutex_type& get_parallel_iteration_mutex() const { return parallel_iter_mutex; }

	// Documentation and signature for std::map wrapping methods were taken from
	// http://en.cppreference.com/w/cpp/container/map

	this_map_type& operator=(const this_map_type& value)
	{
		vals.operator=(value.vals);
		return *this;
	}
	this_map_type& operator=(this_map_type&& value)
	{
		vals.operator=(value.vals);
		return *this;
	}

	/// Returns the allocator associated with the container.
	allocator_type get_allocator() const { return get_inner_map().get_allocator(); }

	/// Returns a reference to the mapped value of the element with key equivalent to key.
	/// If no such element exists, an exception of type std::out_of_range is thrown.
	mapped_type& at(const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().at(key);
	}

	/// Returns a reference to the mapped value of the element with key equivalent to key.
	/// If no such element exists, an exception of type std::out_of_range is thrown.
	const mapped_type& at(const key_type& key) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().at(key);
	}

	/// Returns a reference to the value that is mapped to a key equivalent to key,
	/// performing an insertion if such key does not already exist.
	mapped_type& operator[](const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map()[key];
	}

	/// Returns a reference to the value that is mapped to a key equivalent to key,
	/// performing an insertion if such key does not already exist.
	mapped_type& operator[](key_type&& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map()[key];
	}

	/// Returns an iterator to the first element of the container.
	/// If the container is empty, the returned iterator will be equal to end().
	iterator begin() noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().begin());
	}

	/// Returns an iterator to the first element of the container.
	/// If the container is empty, the returned iterator will be equal to end().
	const_iterator begin() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().begin());
	}

	/// Returns an iterator to the first element of the container.
	/// If the container is empty, the returned iterator will be equal to end().
	const_iterator cbegin() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().cbegin());
	}

	/// Returns an iterator to the element following the last element of the container.
	/// This element acts as a placeholder; attempting to access it results in undefined behavior.
	iterator end() noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().end());
	}

	/// Returns an iterator to the element following the last element of the container.
	/// This element acts as a placeholder; attempting to access it results in undefined behavior.
	const_iterator end() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().end());
	}

	/// Returns an iterator to the element following the last element of the container.
	/// This element acts as a placeholder; attempting to access it results in undefined behavior.
	const_iterator cend() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().cend());
	}

	/// Returns a reverse iterator to the first element of the reversed container.
	/// It corresponds to the last element of the non-reversed container.
	reverse_iterator rbegin() noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().rbegin());
	}

	/// Returns a reverse iterator to the first element of the reversed container.
	/// It corresponds to the last element of the non-reversed container.
	const_reverse_iterator rbegin() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().rbegin());
	}

	/// Returns a reverse iterator to the first element of the reversed container.
	/// It corresponds to the last element of the non-reversed container.
	const_reverse_iterator crbegin() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().crbegin());
	}

	/// Returns a reverse iterator to the element following the last element of the reversed container.
	/// It corresponds to the element preceding the first element of the non-reversed container.
	/// This element acts as a placeholder, attempting to access it results in undefined behavior.
	reverse_iterator rend() noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().rend());
	}

	/// Returns an iterator to the element following the last element of the container.
	/// This element acts as a placeholder; attempting to access it results in undefined behavior.
	const_reverse_iterator rend() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().rend());
	}

	/// Returns an iterator to the element following the last element of the container.
	/// This element acts as a placeholder; attempting to access it results in undefined behavior.
	const_reverse_iterator crend() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().crend());
	}

	/// Checks if the container has no elements, i.e. whether begin() == end().
	bool empty() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().empty();
	}

	/// Returns the number of elements in the container, i.e. std::distance(begin(), end()).
	size_type size() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().size();
	}

	/// Returns the maximum number of elements the container is able to hold due to system or library implementation
	/// limitations, i.e. std::distance(begin(), end()) for the largest container.
	size_type max_size() const noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().max_size();
	}

	/// Removes all elements from the container. Invalidates any references, pointers, or iterators referring to
	/// contained elements. Any past-the-end iterator remains valid.
	void clear() noexcept
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		get_inner_map().clear();
	}

	/// Inserts element(s) into the container, if the container doesn't already contain an element with an
	/// equivalent key.
	template <typename... TFuncArgs>
	auto insert(TFuncArgs... args)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().insert(args...));
	}

	/// Inserts a new element into the container constructed in-place with the given args if there is no element
	/// with the key in the container.
	template <typename... TFuncArgs>
	std::pair<iterator, bool> emplace(TFuncArgs&&... args)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().emplace(args...));
	}

	/// Inserts a new element to the container as close as possible to the position just before hint. The element is
	/// constructed in-place, i.e. no copy or move operations are performed.
	template <typename... TFuncArgs>
	iterator emplace_hint(const_iterator hint, TFuncArgs&&... args)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().emplace_hint(args...));
	}

	/// Removes specified elements from the container.
	template <typename... TFuncArgs>
	auto erase(TFuncArgs... args)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().erase(args...);
	}

	/// Exchanges the contents of the container with those of other. Does not invoke any move, copy, or swap
	/// operations on individual elements.
	void swap(this_map_type& other)
	{
		auto& other_mutex = other.get_parallel_iteration_mutex();
		if (&parallel_iter_mutex == &other_mutex) {
			return;
		}

		std::shared_lock<shared_mutex_type> this_read_lock{parallel_iter_mutex};
		std::shared_lock<shared_mutex_type> other_read_lock{other_mutex};
		get_inner_map().swap(other);
	}

	/// Returns the number of elements with key that compares equivalent to the specified argument, which is either
	/// 1 or 0 since this container does not allow duplicates.
	template <typename TFuncArg>
	size_type count(const TFuncArg& x) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return get_inner_map().count(x);
	}

	/// Finds an element with key equivalent to key.
	iterator find(const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().find(key));
	}

	/// Finds an element with key equivalent to key.
	const_iterator find(const key_type& key) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().find(key));
	}

	/// Finds an element with key that compares equivalent to the value x. This overload only participates in
	/// overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type. It allows
	/// calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	iterator find(const TFuncArg& x)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().find(x));
	}

	/// Finds an element with key that compares equivalent to the value x. This overload only participates in
	/// overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type. It allows
	/// calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	const_iterator find(const TFuncArg& x) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().find(x));
	}

	/// Returns a range containing all elements with the given key in the container. The range is defined by two
	/// iterators, one pointing to the first element that is not less than key and another pointing to the first
	/// element greater than key. Alternatively, the first iterator may be obtained with lower_bound(), and the
	/// second with upper_bound().
	std::pair<iterator, iterator> equal_range(const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().equal_range(key));
	}

	/// Returns a range containing all elements with the given key in the container. The range is defined by two
	/// iterators, one pointing to the first element that is not less than key and another pointing to the first
	/// element greater than key. Alternatively, the first iterator may be obtained with lower_bound(), and the
	/// second with upper_bound().
	std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().equal_range(key));
	}

	/// Returns a range containing all elements with the given key in the container. The range is defined by two
	/// iterators, one pointing to the first element that is not less than key and another pointing to the first
	/// element greater than key. Alternatively, the first iterator may be obtained with lower_bound(), and the
	/// second with upper_bound().
	template <typename TFuncArg>
	std::pair<iterator, iterator> equal_range(const TFuncArg& x)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().equal_range(x));
	}

	/// Returns a range containing all elements with the given key in the container. The range is defined by two
	/// iterators, one pointing to the first element that is not less than key and another pointing to the first
	/// element greater than key. Alternatively, the first iterator may be obtained with lower_bound(), and the
	/// second with upper_bound().
	template <typename TFuncArg>
	std::pair<const_iterator, const_iterator> equal_range(const TFuncArg& x) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().equal_range(x));
	}

	/// Returns an iterator pointing to the first element that is not less than key.
	iterator lower_bound(const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().lower_bound(key));
	}

	/// Returns an iterator pointing to the first element that is not less than key.
	const_iterator lower_bound(const key_type& key) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().lower_bound(key));
	}

	/// Returns an iterator pointing to the first element that compares not less to the value x. This overload only
	/// participates in overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type.
	/// They allow calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	iterator lower_bound(const TFuncArg& x)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().lower_bound(x));
	}

	/// Returns an iterator pointing to the first element that compares not less to the value x. This overload only
	/// participates in overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type.
	/// They allow calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	const_iterator lower_bound(const TFuncArg& x) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().lower_bound(x));
	}

	/// Returns an iterator pointing to the first element that is greater than key.
	iterator upper_bound(const key_type& key)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().upper_bound(key));
	}

	/// Returns an iterator pointing to the first element that is greater than key.
	const_iterator upper_bound(const key_type& key) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().upper_bound(key));
	}

	/// Returns an iterator pointing to the first element that compares greater to the value x. This overload only
	/// participates in overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type.
	/// They allow calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	iterator upper_bound(const TFuncArg& x)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().upper_bound(x));
	}

	/// Returns an iterator pointing to the first element that compares greater to the value x. This overload only
	/// participates in overload resolution if the qualified-id Compare::is_transparent is valid and denotes a type.
	/// They allow calling this function without constructing an instance of TFuncArg.
	template <typename TFuncArg>
	const_iterator upper_bound(const TFuncArg& x) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		return make_iterator(get_inner_map().upper_bound(x));
	}

	/// Returns the function object that compares the keys, which is a copy of this container's constructor argument
	/// comp.
	key_compare key_comp() const { return get_inner_map().key_comp(); }

	/// Returns a function object that compares objects of type value_type (key-value pairs) by using
	/// key_comp to compare the first components of the pairs.
	value_compare value_comp() const { return get_inner_map().value_comp(); }

	/// Applies the given action to each element in this map.
	/// The action is not applied to elements simultaneously.
	/// An action is a function object with signature `void(const K&, V&, unsigned int)`
	/// where the first parameter is the value that the action takes and the second
	/// parameter is a dummy value.
	template <typename TAction>
	void serial_for(const TAction& action)
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		::stride::util::parallel::serial_for<key_type, mapped_type, TAction>(get_inner_map(), action);
	}

	/// Applies the given action to each element in this map.
	/// The action is not applied to elements simultaneously.
	/// An action is a function object with signature `void(const K&, const V&, unsigned int)`
	/// where the first parameter is the value that the action takes and the second
	/// parameter is a dummy value.
	template <typename TAction>
	void serial_for(const TAction& action) const
	{
		std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
		::stride::util::parallel::serial_for<key_type, mapped_type, TAction>(get_inner_map(), action);
	}

	/// Applies the given action to each element in this map.
	/// The action may be applied to up to `num_threads` elements simultaneously.
	/// An action is a function object with signature `void(const K&, V&, unsigned int)`
	/// where the first parameter is the value that the action takes and the third
	/// parameter is the index of the thread it runs on.
	template <typename TAction, typename TCreateChunks = CreateChunks<key_type>>
	void parallel_for(unsigned int num_threads, const TAction& action)
	{
		if (empty()) {
			// Nothing to do here.
			return;
		} else if (num_threads <= 1) {
			// Use a simple serial implementation.
			serial_for<TAction>(action);
			return;
		}

		// We can't divide an `std::map` into chunks like we would divide an `std::vector`
		// (even though the map's underlying data structure probably supports it). Instead,
		// we'll find the min and max keys in the map and use those to carve the map into
		// `num_thread` chunks using a hack: we insert dummy keys, get an iterator to
		// the next actual value in the map and take that value as the starting point of
		// a thread.
		//
		// Note that this technique mutates the map so we should acquire a lock to ensure
		// thread-safety.

		auto min_key = begin()->first;
		auto max_key = rbegin()->first;
		auto chunks = TCreateChunks()(max_key - min_key + 1, num_threads);
		std::vector<typename inner_map_type::iterator> start_iterators;
		{
			std::unique_lock<shared_mutex_type> write_lock{parallel_iter_mutex};

			auto start_value = min_key;
			for (std::size_t i = 0; i < chunks.size(); i++) {
				auto start_iterator = vals.find(start_value);
				if (start_iterator == vals.end()) {
					// Perform the insert hack: insert a value,
					// get an iterator to the inserted value, proceed to the
					// next value and erase the inserted value.
					auto inserted_elem_iterator = vals.emplace(start_value, mapped_type()).first;
					start_iterator = inserted_elem_iterator;
					start_iterator++;
					vals.erase(inserted_elem_iterator);
				}

				start_iterators.push_back(start_iterator);
				start_value = min_key + chunks[i];
			}
		}

		// We now want to start a bunch of threads.
		std::vector<std::thread> thread_pool;
		for (std::size_t i = 0; i < chunks.size(); i++) {
			auto start_iterator = start_iterators[i];
			auto end_iterator = i == chunks.size() - 1 ? vals.end() : start_iterators[i + 1];

			// Only start jobs if we know that they're non-empty.
			if (start_iterator != vals.end() && start_iterator != end_iterator) {
				thread_pool.emplace_back([this, &action, i, start_iterator, end_iterator] {
					std::shared_lock<shared_mutex_type> read_lock{parallel_iter_mutex};
					for (auto it = start_iterator; it != end_iterator; it++) {
						auto& elem = *it;
						action(elem.first, elem.second, i);
					}
				});
			}
		}

		// Wait for the threads to finish.
		for (auto& thread : thread_pool) {
			thread.join();
		}
	}

	/// Applies the given action to each element in this map.
	/// The action may be applied to up to `num_threads` elements simultaneously.
	/// An action is a function object with signature `void(const K&, const V&, unsigned int)`
	/// where the first parameter is the value that the action takes and the third
	/// parameter is the index of the thread it runs on.
	template <typename TAction, typename TCreateChunks = CreateChunks<key_type>>
	void parallel_for(unsigned int num_threads, const TAction& action) const
	{
		const_cast<this_map_type*>(this)->parallel_for(
		    num_threads,
		    [&action](const key_type& key, const mapped_type& value, unsigned int thread_number) -> void {
			    action(key, value, thread_number);
		    });
	}
};

template <typename... TArgs>
bool operator==(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return true;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() == rhs.get_inner_map();
}

template <typename... TArgs>
bool operator!=(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return false;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() != rhs.get_inner_map();
}

template <typename... TArgs>
bool operator<(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return false;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() < rhs.get_inner_map();
}

template <typename... TArgs>
bool operator<=(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return true;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() <= rhs.get_inner_map();
}

template <typename... TArgs>
bool operator>(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return false;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() > rhs.get_inner_map();
}

template <typename... TArgs>
bool operator>=(const ParallelMap<TArgs...>& lhs, const ParallelMap<TArgs...>& rhs)
{
	using shared_mutex_type = typename decltype(lhs)::shared_mutex_type;

	auto& lhs_mutex = lhs.parallel_iter_mutex();
	auto& rhs_mutex = rhs.parallel_iter_mutex();
	if (&lhs_mutex == &rhs_mutex) {
		return true;
	}

	std::shared_lock<shared_mutex_type> lhs_read_lock{lhs_mutex};
	std::shared_lock<shared_mutex_type> rhs_read_lock{rhs_mutex};
	return lhs.get_inner_map() >= rhs.get_inner_map();
}

/// Applies the given action to each element in the given map.
/// The action is not applied to elements simultaneously.
/// An action is a function object with signature `void(const K&, V&, unsigned int)`
/// where the first parameter is the value that the action takes and the second
/// parameter is a dummy value.
template <typename K, typename V, typename TAction>
void serial_for(ParallelMap<K, V>& values, const TAction& action)
{
	values.serial_for(action);
}

/// Applies the given action to each element in the given map.
/// The action may be applied to up to `num_threads` elements simultaneously.
/// An action is a function object with signature `void(const K&, V&, unsigned int)`
/// where the first parameter is the value that the action takes and the third
/// parameter is the index of the thread it runs on.
template <typename K, typename V, typename TAction>
void parallel_for(ParallelMap<K, V>& values, unsigned int number_of_threads, const TAction& action)
{
	values.parallel_for(number_of_threads, action);
}

/// Applies the given action to each element in the given map.
/// The action is not applied to elements simultaneously.
/// An action is a function object with signature `void(const K&, V&, unsigned int)`
/// where the first parameter is the value that the action takes and the second
/// parameter is a dummy value.
template <typename K, typename V, typename TAction>
void serial_for(const ParallelMap<K, V>& values, const TAction& action)
{
	values.serial_for(action);
}

/// Applies the given action to each element in the given map.
/// The action may be applied to up to `num_threads` elements simultaneously.
/// An action is a function object with signature `void(const K&, V&, unsigned int)`
/// where the first parameter is the value that the action takes and the third
/// parameter is the index of the thread it runs on.
template <typename K, typename V, typename TAction>
void parallel_for(const ParallelMap<K, V>& values, unsigned int number_of_threads, const TAction& action)
{
	values.parallel_for(number_of_threads, action);
}
}

template <typename... TArgs>
void swap(stride::util::parallel::ParallelMap<TArgs...>& lhs, stride::util::parallel::ParallelMap<TArgs...>& rhs)
{
	lhs.swap(rhs);
}

template <typename... TArgs>
void swap(
    typename stride::util::parallel::ParallelMap<TArgs...>::iterator& lhs,
    typename stride::util::parallel::ParallelMap<TArgs...>::iterator& rhs)
{
	lhs.swap(rhs);
}

template <typename... TArgs>
void swap(
    typename stride::util::parallel::ParallelMap<TArgs...>::const_iterator& lhs,
    typename stride::util::parallel::ParallelMap<TArgs...>::const_iterator& rhs)
{
	lhs.swap(rhs);
}

template <typename... TArgs>
void swap(
    typename stride::util::parallel::ParallelMap<TArgs...>::reverse_iterator& lhs,
    typename stride::util::parallel::ParallelMap<TArgs...>::reverse_iterator& rhs)
{
	lhs.swap(rhs);
}

template <typename... TArgs>
void swap(
    typename stride::util::parallel::ParallelMap<TArgs...>::const_reverse_iterator& lhs,
    typename stride::util::parallel::ParallelMap<TArgs...>::const_reverse_iterator& rhs)
{
	lhs.swap(rhs);
}
}
}

#endif