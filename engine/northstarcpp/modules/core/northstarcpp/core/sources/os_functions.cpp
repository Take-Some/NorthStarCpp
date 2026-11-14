////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/os_include.h>

u64 windows_query_performance_counter	()
{
	u64						result;
	QueryPerformanceCounter	((PLARGE_INTEGER)&result);
	return					(result);
}
