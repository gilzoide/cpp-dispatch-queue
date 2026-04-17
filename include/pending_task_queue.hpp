#pragma once

#include <deque>
#include <unordered_map>

#include "pending_task.hpp"


namespace dispatch_queue {

class pending_task_queue {
public:
	bool empty() const;
	size_t size() const;
	void clear();

	pending_task* find(task_id id);
	const pending_task* find(task_id id) const;

	void push(pending_task&& task, task_id dependency = 0);
	pending_task* pop();
	int process_completed_task(const pending_task* completed_task);

private:
	std::deque<pending_task*> ready_pending_tasks;
	std::unordered_map<task_id, pending_task> pending_tasks;
};

}
