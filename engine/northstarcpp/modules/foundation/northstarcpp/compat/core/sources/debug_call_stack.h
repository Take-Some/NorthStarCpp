////////////////////////////////////////////////////////////////////////////
//	Created 	: 15.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef DEBUG_CALL_STACK_H_INCLUDED
#define DEBUG_CALL_STACK_H_INCLUDED

#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace core {
namespace debug {
namespace call_stack {

typedef ::fastdelegate::FastDelegate<
			bool (
				u32,	// call stack id ( 0 means the calling function )
				u32,	// num_call_stack_lines
				pcstr,	// module
				pcstr,	// file
				int,	// line number
				pcstr,	// function,
				u32		// address
			)
		>		Callback;

void	iterate	( _EXCEPTION_POINTERS*	pointers,  // can be NULL
				  void*					callstack[], // can be NULL
				  Callback const&		callback,
				  bool					invert_order = false);

void	get_stack_trace (void*	stacktrace[], 
						 u32	stacktrace_max, 
						 u32	num_to_capture, 
						 u32*	out_hash);

void	finalize_symbols( );

} // namespace call_stack
} // namespace debug
} // namespace core
} // namespace northstarcpp

#endif // #ifndef DEBUG_CALL_STACK_H_INCLUDED