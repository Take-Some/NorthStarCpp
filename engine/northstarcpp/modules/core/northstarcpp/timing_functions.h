////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_TIMING_FUNCTIONS_H_INCLUDED
#define NORTHSTARCPP_TIMING_FUNCTIONS_H_INCLUDED

#if NORTHSTARCPP_PLATFORM_WINDOWS
#	include <intrin.h>
#	pragma intrinsic(__rdtsc)
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS

namespace northstarcpp {
namespace timing {

NORTHSTARCPP_CORE_API	extern u64	g_qpc_per_second;
NORTHSTARCPP_CORE_API	u64			query_performance_counter	( );

inline			u64			get_clocks					( )
{
#if NORTHSTARCPP_PLATFORM_WINDOWS
	return		( __rdtsc( ) );
#else // #if NORTHSTARCPP_PLATFORM_WINDOWS
	return		( query_performance_counter( ) );
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS
}

inline u64					get_QPC						( )
{
	return		( query_performance_counter( ) );
}

} // namespace timing
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_TIMING_FUNCTIONS_H_INCLUDED