////////////////////////////////////////////////////////////////////////////
//	Created 	: 15.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

struct _EXCEPTION_POINTERS;
#include "debug_call_stack.h"

void northstarcpp::core::debug::call_stack::iterate	( _EXCEPTION_POINTERS* pointers,  // can be NULL
											  void* call_stack[], // can be NULL
											  Callback const& callback,
											  bool			invert_order)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS	( pointers, call_stack, invert_order );
	NORTHSTARCPP_UNREFERENCED_PARAMETER		( callback );
}

void northstarcpp::core::debug::call_stack::get_stack_trace ( void* stack_trace[], u32 const stack_trace_max, u32 const num_to_capture, u32* out_hash )
{
#pragma message( NORTHSTARCPP_TODO("PS3: iterate call stack") )
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( stack_trace, stack_trace_max, num_to_capture, out_hash );
}

void northstarcpp::core::debug::call_stack::finalize_symbols( )
{
}