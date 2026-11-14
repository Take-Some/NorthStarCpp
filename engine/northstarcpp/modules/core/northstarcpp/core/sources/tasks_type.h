////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TASKS_TYPE_H
#define TASKS_TYPE_H

#include <northstarcpp/intrusive_list.h>
#include <northstarcpp/intrusive_queue.h>
#include "tasks_allocator.h"
#include <northstarcpp/tasks_system.h>

namespace northstarcpp {
namespace tasks {

class task_type
{
public:
							task_type			();
							task_type			(pcstr description, task_type_flags_enum const flags);

	u64						min_tasks_ordinal	() const { return m_min_task_ordinal; }
	task_type_flags_enum	flags				() const { return (task_type_flags_enum)m_flags; }
	typedef intrusive_queue<task, task, & task::m_next_task_in_type_queue>		tasks_type_queue;

private:
	tasks_type_queue							m_tasks;
//	NORTHSTARCPP_CACHE_LINE_PAD;
	threading::atomic64_type					m_min_task_ordinal;

public: // really private
	task_type *									m_next_task_type;

private:
	pcstr										m_description;
	char										m_flags;

	friend					class				task_manager;
};

typedef			intrusive_list<task_type, task_type, & task_type::m_next_task_type>		task_type_list_container;

} // namespace tasks
} // namespace northstarcpp

#endif // #ifndef TASKS_TYPE_H
