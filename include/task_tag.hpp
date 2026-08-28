#pragma once

#include <cstddef>
#include <limits>

namespace dispatch_queue {

/**
 * Tags used by `dispatch_queue::dispatch_tagged`.
 * Tasks tagged with the same value never run in parallel: at most one task is processed for each tag at a time.
 */
using task_tag = int;

/**
 * Special tag value that represents tasks that are not tagged at all.
 */
static constexpr task_tag NULL_TAG = std::numeric_limits<task_tag>::min();

}
