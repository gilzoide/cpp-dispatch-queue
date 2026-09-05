#pragma once

#ifdef __cpp_lib_ranges
	#include <ranges>
#else
	#include <iterator>
#endif

namespace dispatch_queue {

namespace detail {

template<typename R>
auto range_begin(R& r) {
#ifdef __cpp_lib_ranges
	return std::ranges::begin(r);
#else
	return std::begin(r);
#endif
}

template<typename R>
auto range_end(R& r) {
#ifdef __cpp_lib_ranges
	return std::ranges::end(r);
#else
	return std::end(r);
#endif
}

template<typename R>
auto range_size(const R& r) {
#ifdef __cpp_lib_ranges
	return std::ranges::size(r);
#elif defined(__cpp_lib_nonmember_container_access)
	return std::size(r);
#else
	return std::distance(range_begin(r), range_end(r));
#endif
}

template<typename I, typename S>
auto it_distance(I&& first, S&& last) {
#ifdef __cpp_lib_ranges
	return std::ranges::distance(first, last);
#else
	return std::distance(first, last);
#endif
}

template<typename I, typename D>
auto it_next(I&& first, D&& distance) {
#ifdef __cpp_lib_ranges
	return std::ranges::next(first, distance);
#else
	return std::next(first, distance);
#endif
}

template<typename It, typename F>
void apply_batches(F&& f, const It& begin, const It& end, size_t batch_size) {
	auto size = it_distance(begin, end);
	auto batch_begin = begin;
	for (size_t i = 0; i < size - static_cast<ssize_t>(batch_size); i += batch_size) {
		auto batch_end = it_next(batch_begin, batch_size);
		f(batch_begin, batch_end);
		batch_begin = batch_end;
	}
	if (batch_begin != end) {
		f(batch_begin, end);
	}
}

template<typename R, typename F>
void apply_batches(F&& f, R&& range, size_t batch_size) {
	auto size = range_size(range);
	auto batch_begin = range_begin(range);
	for (size_t i = 0; i < size - static_cast<ssize_t>(batch_size); i += batch_size) {
		auto batch_end = it_next(batch_begin, batch_size);
		f(batch_begin, batch_end);
		batch_begin = batch_end;
	}
	auto end = range_end(range);
	if (batch_begin != end) {
		f(batch_begin, end);
	}
}

} // end namespace detail

} // end namespace dispatch_queue
