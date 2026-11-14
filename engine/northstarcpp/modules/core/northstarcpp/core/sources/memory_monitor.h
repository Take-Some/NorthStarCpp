////////////////////////////////////////////////////////////////////////////
//	Created 	: 09.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_MONITOR_H_INCLUDED
#define MEMORY_MONITOR_H_INCLUDED

#if defined(NDEBUG) || !defined(_MSC_VER)
#	define NORTHSTARCPP_USE_MEMORY_MONITOR	0
#else // #ifdef NDEBUG
#	define NORTHSTARCPP_USE_MEMORY_MONITOR	1
#endif // #ifdef NDEBUG

#if NORTHSTARCPP_USE_MEMORY_MONITOR

namespace northstarcpp {
namespace memory {
namespace monitor {

		void	initialize				( );
		void	finalize				( );

		void	on_alloc				( pvoid& pointer, size_t& size, size_t previous_size, pcstr description );
		void	on_free					( pvoid& pointer, bool can_clear = true );

inline	u32		get_house_keeping_size	( ) { return 0; }

} // namespace monitor
} // namespace memory
} // namespace northstarcpp

#endif // #if NORTHSTARCPP_USE_MEMORY_MONITOR

#endif // #ifndef MEMORY_MONITOR_H_INCLUDED