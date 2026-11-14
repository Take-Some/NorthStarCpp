////////////////////////////////////////////////////////////////////////////
//	Created		: 30.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_GUARD_H_INCLUDED
#define MEMORY_GUARD_H_INCLUDED

#ifdef NDEBUG
#	define NORTHSTARCPP_USE_MEMORY_GUARD	0
#else // #ifdef NDEBUG
#	define NORTHSTARCPP_USE_MEMORY_GUARD	1
#endif // #ifdef NDEBUG

#if NORTHSTARCPP_USE_MEMORY_GUARD

namespace northstarcpp {
namespace memory {
namespace guard {

void	initialize				( );
void	finalize				( );

void	on_alloc				( pvoid& pointer, size_t& size, size_t previous_size, pcstr description );
void	on_free					( pvoid& pointer, bool can_clear = true );

size_t	get_house_keeping_size	( );
size_t	get_buffer_size			( pvoid buffer );

} // namespace guard
} // namespace memory
} // namespace northstarcpp

#endif // #if NORTHSTARCPP_USE_MEMORY_GUARD

#endif // #ifndef MEMORY_GUARD_H_INCLUDED