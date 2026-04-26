#pragma once

#include <type_traits>

namespace dispatch_queue {

namespace detail {

template<class T, template<class...> class U>
struct is_instance_of : public std::false_type {};

template<template<class...> class U, class... Vs>
struct is_instance_of<U<Vs...>,U> : public std::true_type {};

} // end namespace detail

} // end namespace dispatch_queue
