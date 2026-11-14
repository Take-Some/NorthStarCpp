////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

using northstarcpp::memory::reader;

pcstr reader::r_string		( )
{
	pcstr const result	= (pcstr)m_pointer;
	skip_string			( );
	return				( result );
}

void reader::skip_string	( )
{
	pcbyte i			= m_pointer;
	pcbyte e			= m_data + m_size;
	for ( ; (i != e) && *i; ++i );
	ASSERT				( i != e );
	m_pointer			= i + 1;
}