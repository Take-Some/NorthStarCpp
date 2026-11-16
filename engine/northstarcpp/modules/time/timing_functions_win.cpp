////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2009
//	Author		: Kayla Verner
//  Copyright (C) Kayla's Systems - 2009
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "../core/pch.h"

#ifdef NORTHSTARCPP_STANDALONE_MATH
// Native 2026 runtime retains the original QueryPerformanceCounter timer ABI.
// Initialization is independent of DLL load order, and does not alter global
// Windows timer resolution or floating-point CPU control state.
#include <northstarcpp/timing_functions.h>
#include "timing.h"
#include <windows.h>
#include <stdexcept>
namespace northstarcpp::timing {
u64 g_qpc_per_second=[]() -> u64 {
    LARGE_INTEGER frequency{};
    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)
        return 0;
    return static_cast<u64>(frequency.QuadPart);
}();
void initialize() {
    // Preserved engine entry point; frequency is initialized before use.
}
u64 query_performance_counter() {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value))
        throw std::runtime_error("QueryPerformanceCounter failed");
    return static_cast<u64>(value.QuadPart);
}
} // namespace northstarcpp::timing
#else // Original 2009 platform initialization, retained for source reference.
#include "../core/pch.h"
#include "timing.h"
#include "platform_extensions_win.h"
#include <northstarcpp/os_include.h>

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
#endif // NORTHSTARCPP_STANDALONE_MATH
