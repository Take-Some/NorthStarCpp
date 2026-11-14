////////////////////////////////////////////////////////////////////////////
//	Created		: 25.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TASKS_THREAD_TLS_H_INCLUDED
#define TASKS_THREAD_TLS_H_INCLUDED

#include <northstarcpp/compat/tasks_aware_event.h>
#include <northstarcpp/compat/tasks_task.h>

namespace northstarcpp {
namespace tasks {

class thread_pool;

class thread_tls : private boost::noncopyable
{
public:
	enum type_enum				{ type_task_thread, type_user_thread };
	enum state_enum				{ state_active, state_inactive, state_locked };

public:
	thread_tls					() : thread_type(type_task_thread), pool(NULL), thread_index(0),
									 thread_id(0), hardware_thread(0), current_task(NULL), 
									 last_task_type(NULL), state(state_inactive), in_grab_next_task(0) {}
	bool						is_task_thread () const { return thread_type == type_task_thread; }
	bool						is_user_thread () const { return thread_type == type_user_thread; }

	void						thread_proc_impl();
	void						thread_proc		();

public:
	NORTHSTARCPP_MAX_CACHE_LINE_PAD;
	threading::event			event_should_work;
	threading::atomic32_type	state;
	NORTHSTARCPP_MAX_CACHE_LINE_PAD;
	task						user_thread_root_task;
	threading::event			event_start_thread_work;
	threading::event			event_pause_ended;
	tasks::event				event_wait_for_children;
	thread_pool *				pool;
	task *						current_task;
	task_type *					last_task_type;
	u32							thread_index;
	u32							thread_id;
	u32							hardware_thread;
	type_enum					thread_type;
	threading::atomic32_type	in_grab_next_task;
};

NORTHSTARCPP_CORE_API	thread_tls *	current_task_thread_tls		();

// log through the tasking system logging wrapper
NORTHSTARCPP_CORE_API	void			log							(thread_tls * tls, pcstr format, ...);

} // namespace northstarcpp
} // namespace tasks

#endif // #ifndef TASKS_THREAD_TLS_H_INCLUDED