////////////////////////////////////////////////////////////////////////////
//	Created		: 30.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_LEAK_DETECTOR_H_INCLUDED
#define MEMORY_LEAK_DETECTOR_H_INCLUDED

#if defined(NDEBUG) || !defined(_MSC_VER)
#	define NORTHSTARCPP_USE_MEMORY_LEAK_DETECTOR	0
#else // #if defined(NDEBUG) || !defined(_MSC_VER)
#	define NORTHSTARCPP_USE_MEMORY_LEAK_DETECTOR	1
#endif // #if defined(NDEBUG) || !defined(_MSC_VER)

#if NORTHSTARCPP_USE_MEMORY_LEAK_DETECTOR

namespace northstarcpp {
namespace memory {
namespace leak_detector {

void	preinitialize			( );
void	initialize				( );
void	finalize				( );

void	on_alloc				( pvoid& pointer, size_t& size, size_t previous_size, pcstr description );
void	on_free					( pvoid& pointer, bool can_clear = true );

size_t	get_house_keeping_size	( );

} // namespace leak_detector
} // namespace memory
} // namespace northstarcpp

#endif // #if NORTHSTARCPP_USE_MEMORY_LEAK_DETECTOR

#endif // #ifndef MEMORY_LEAK_DETECTOR_H_INCLUDED