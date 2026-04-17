#include "../include/pending_task_queue.hpp"
#include "pending_task.hpp"

#include <cassert>

namespace dispatch_queue {

bool pending_task_queue::empty() const {
	return ready_pending_tasks.empty();
}

size_t pending_task_queue::size() const {
	return ready_pending_tasks.size();
}

void pending_task_queue::clear() {
	ready_pending_tasks.clear();
}

pending_task* pending_task_queue::find(task_id id) {
	auto it = pending_tasks.find(id);
	if (it != pending_tasks.end()) {
		return &it->second;
	}
	else {
		return nullptr;
	}
}

const pending_task* pending_task_queue::find(task_id id) const {
	auto it = pending_tasks.find(id);
	if (it != pending_tasks.end()) {
		return &it->second;
	}
	else {
		return nullptr;
	}
}

void pending_task_queue::push(pending_task&& task, task_id dependency) {
	auto it_success_pair = pending_tasks.emplace(task.id, std::move(task));
	assert(it_success_pair.second && "FIXME: pending_tasks should never have a task.id already registered");

	pending_task* emplaced_task = &it_success_pair.first->second;
	if (auto dependency_task = find(dependency)) {
		dependency_task->continuations.push_back(emplaced_task);
	}
	else {
		ready_pending_tasks.push_back(emplaced_task);
	}
}

pending_task* pending_task_queue::pop() {
	if (!ready_pending_tasks.empty()) {
		pending_task* front = ready_pending_tasks.front();
		ready_pending_tasks.pop_front();
		return front;
	}
	else {
		return nullptr;
	}
}

int pending_task_queue::process_completed_task(const pending_task* completed_task) {
	int new_task_count = completed_task->continuations.size();
	std::copy(completed_task->continuations.begin(), completed_task->continuations.end(), std::back_inserter(ready_pending_tasks));
	pending_tasks.erase(completed_task->id);
	return new_task_count;
}

}
