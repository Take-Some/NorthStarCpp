////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2009
//	Author		: Kayla Verner
//  Copyright (C) Kayla's Systems - 2009
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "timing.h"
#include "platform_extensions_win.h"
#include <northstarcpp/compat/os_include.h>

#pragma warning(push)
#pragma warning(disable: 4201)
#include <mmsystem.h>
#pragma warning(pop)

#pragma comment(lib, "winmm.lib")

u64	northstarcpp::timing::g_qpc_per_second	= 0;

void northstarcpp::timing::initialize					( )
{
	timeBeginPeriod				( 1 );

	bool const result			= platform::unload_delay_loaded_library( "winmm.dll" );
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( result );
	R_ASSERT					( result );

	QueryPerformanceFrequency	( ( PLARGE_INTEGER )&g_qpc_per_second );

	errno_t const error			= _controlfp_s( 0, _PC_24, MCW_PC );
	R_ASSERT_U					( !error );
}

u64 northstarcpp::timing::query_performance_counter	( )
{
	u64							result;
	QueryPerformanceCounter		( ( PLARGE_INTEGER )&result );
	return						(result);
}