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
auto range_size(const R& r) {
#ifdef __cpp_lib_ranges
	return std::ranges::size(r);
#elif defined(__cpp_lib_nonmember_container_access)
	return std::size(r);
#else
	return std::distance(range_begin(r), range_end(r));
#endif
}

} // end namespace detail

} // end namespace dispatch_queue
