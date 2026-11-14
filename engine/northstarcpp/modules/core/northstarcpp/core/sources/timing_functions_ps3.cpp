////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/timing_functions.h>
#include "timing.h"
#include <sys/sys_time.h>
#include <sys/time_util.h>

u64	northstarcpp::timing::g_qpc_per_second	= 0; // should be 79 800 000

void northstarcpp::timing::initialize				( )
{
	g_qpc_per_second	= sys_time_get_timebase_frequency( );
}

u64 northstarcpp::timing::query_performance_counter	( )
{
	// mftb instruction has a problem because of a 4 cycle lag between
	// getting the lower 32 bit and higher ones. here we suppose that 
	// sys_time_get_system_time solves this problem like
	// QueryPerformanceCounter does it on Xbox360
//	return				__mftb( );
	register u64		now;
	SYS_TIMEBASE_GET	( now );
	return				now;
}