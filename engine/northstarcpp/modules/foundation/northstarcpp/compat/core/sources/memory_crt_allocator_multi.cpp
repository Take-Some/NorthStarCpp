////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/memory_crt_allocator.h>

using northstarcpp::memory::crt_allocator;

crt_allocator::crt_allocator		( ) :
	m_malloc_ptr				( 0 ),
	m_free_ptr					( 0 )
{
}

size_t crt_allocator::total_size	( ) const
{
	return						( 0 );
}

size_t crt_allocator::allocated_size( ) const
{
	return						( 0 );
}