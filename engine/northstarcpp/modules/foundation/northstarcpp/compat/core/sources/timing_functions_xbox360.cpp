////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "timing.h"
#include <northstarcpp/compat/timing_functions.h>
#include <northstarcpp/compat/os_include.h>
#include <float.h>

u64	northstarcpp::timing::g_qpc_per_second;

void northstarcpp::timing::initialize				( )
{
	QueryPerformanceFrequency	( ( PLARGE_INTEGER )&g_qpc_per_second );
	errno_t const error			= _controlfp_s( 0, _PC_24, MCW_PC );
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( error );
	R_ASSERT					( !error );
}

u64 northstarcpp::timing::query_performance_counter	( )
{
	u64							result;
	QueryPerformanceCounter		( ( PLARGE_INTEGER )&result );
	return						(result);
}