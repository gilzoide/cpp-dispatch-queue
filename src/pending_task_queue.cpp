#include "../include/detail/pending_task_queue.hpp"

namespace dispatch_queue {

namespace detail {

bool pending_task_queue::empty() const {
	return background_tasks.empty();
}

size_t pending_task_queue::size() const {
	return background_tasks.size();
}

void pending_task_queue::clear() {
	background_tasks.clear();
}

bool pending_task_queue::push(task_type type, task_function&& task, task_tag tag) {
	switch (type) {
		case task_type::main:
			main_loop_tasks.push_back({ std::move(task) });
			return false;

		case task_type::tagged:
			if (tag != NULL_TAG) {
#ifdef __cpp_lib_unordered_map_try_emplace
				auto pair = tagged_tasks.try_emplace(tag, std::list<pending_task>{});
#else
				auto pair = tagged_tasks.emplace(tag, std::list<pending_task>{});
#endif
				if (pair.second) {
					// tag didn't exist, task is readily available to be processed
					background_tasks.push_back({ std::move(task), tag });
					return true;
				}
				else {
					// tag exists and is being processed: queue task until tag gets unblocked
					pair.first->second.push_back({ std::move(task), tag });
					return false;
				}
			}
			[[fallthrough]];

		case task_type::background:
			background_tasks.push_back({ std::move(task), NULL_TAG });
			return true;

		default:
			return false;
	}
}

bool pending_task_queue::try_pop(pending_task& task) {
	task_tag previous_tag = task.tag;
	if (previous_tag != NULL_TAG) {
		auto it = tagged_tasks.find(previous_tag);
		if (it->second.empty()) {
			// when last task with tag is processed, erase the tag: this unblocks the tag
			tagged_tasks.erase(it);
		}
		else {
			// otherwise, move the first task for the tag to the end of background_tasks queue
			background_tasks.splice(background_tasks.end(), it->second, it->second.begin());
		}
	}

	if (!background_tasks.empty()) {
		task = std::move(background_tasks.front());
		background_tasks.pop_front();
		return true;
	}
	else {
		task = {};
		return false;
	}
}

std::list<task_function> pending_task_queue::pop_main_loop_tasks() {
	std::list<task_function> result;
	main_loop_tasks.swap(result);
	return result;
}

} // end namespace detail

} // end namespace dispatch_queue
