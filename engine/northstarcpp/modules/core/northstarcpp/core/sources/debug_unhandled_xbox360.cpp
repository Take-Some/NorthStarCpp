////////////////////////////////////////////////////////////////////////////
//	Created 	: 07.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_platform.h"

void northstarcpp::core::debug::platform::format_message			( )
{
}

void northstarcpp::core::debug::platform::save_minidump				( _SYSTEMTIME const& date_time, _EXCEPTION_POINTERS* exception_information )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( date_time );
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( exception_information );
}

void northstarcpp::core::debug::platform::prologue_dump_call_stack	( _EXCEPTION_POINTERS* exception_information )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( exception_information );
}

void northstarcpp::core::debug::set_thread_stack_guarantee			( )
{
}