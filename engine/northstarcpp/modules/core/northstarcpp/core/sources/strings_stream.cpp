////////////////////////////////////////////////////////////////////////////
//	Created		: 08.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/strings_stream.h>
#include <stdarg.h>

using northstarcpp::strings::stream;

void stream::append				( pcstr format, ... )
{
	va_list						mark;
	va_start					( mark, format );

	string4096					buffer;
	int	const size				= vsnprintf(
		buffer,
		sizeof(buffer) - 1,
		format,
		mark
	);
    va_end						( mark );

	if ( !size )
		return;

	if ( m_allocated_size <= (m_buffer_size + size) ) {
		if ( !m_allocated_size )
			m_allocated_size	= 1024;

		while ( m_allocated_size <= (m_buffer_size + size) )
			m_allocated_size	*= 2;

		m_buffer				= static_cast<pbyte>(NORTHSTARCPP_REALLOC_IMPL( *m_allocator, m_buffer, m_allocated_size, "northstarcpp::strings::stream"));
	}

	memory::copy				(m_buffer + m_buffer_size, m_allocated_size - m_buffer_size, buffer, size);
	m_buffer_size				+= size;
}