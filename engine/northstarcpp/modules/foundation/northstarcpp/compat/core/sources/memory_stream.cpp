////////////////////////////////////////////////////////////////////////////
//	Created		: 17.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/memory_stream.h>

using northstarcpp::memory::stream;

void stream::append				( pcvoid const buffer, u32 const buffer_size )
{
	if ( !buffer_size )
		return;

	if ( m_allocated_size <= (m_buffer_size + buffer_size) ) {
		if ( !m_allocated_size )
			m_allocated_size	= 1024;

		while ( m_allocated_size <= (m_buffer_size + buffer_size) )
			m_allocated_size	*= 2;

		m_buffer				= static_cast<pbyte>(NORTHSTARCPP_REALLOC_IMPL( *m_allocator, m_buffer, m_allocated_size, "northstarcpp::memory::stream"));
	}

	memory::copy				(m_buffer + m_buffer_size, m_allocated_size - m_buffer_size, buffer, buffer_size);
	m_buffer_size				+= buffer_size;
}