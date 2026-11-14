////////////////////////////////////////////////////////////////////////////
//	Created		: 05.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef THREADING_FUNCTIONS_H_INCLUDED
#define THREADING_FUNCTIONS_H_INCLUDED

namespace northstarcpp {
namespace threading {

struct thread_entry_params {
	thread_function_type		function_to_call;
	volatile pcstr				thread_name_for_logging;
	volatile pcstr				thread_name_for_debugger;
	u32							hardware_thread;
	threading::tasks_awareness	tasks_awareness;
	bool						processed;
};

} // namespace threading
} // namespace northstarcpp

#endif // #ifndef THREADING_FUNCTIONS_H_INCLUDED