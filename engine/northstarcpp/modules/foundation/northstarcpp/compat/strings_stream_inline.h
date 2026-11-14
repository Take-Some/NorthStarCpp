////////////////////////////////////////////////////////////////////////////
//	Created		: 08.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_STRINGS_STREAM_INLINE_H_INCLUDED
#define NORTHSTARCPP_STRINGS_STREAM_INLINE_H_INCLUDED

inline northstarcpp::strings::stream::stream				( memory::base_allocator* allocator ) :
	m_allocator		( allocator ),
	m_buffer		( 0 ),
	m_buffer_size	( 0 ),
	m_allocated_size( 0 )
{
}

inline northstarcpp::strings::stream::~stream				( )
{
	NORTHSTARCPP_FREE_IMPL	( *m_allocator, m_buffer );
}

inline pcbyte northstarcpp::strings::stream::get_buffer		( ) const
{
	return			m_buffer;
}

inline u32 northstarcpp::strings::stream::get_buffer_size	( ) const
{
	return			m_buffer_size;
}

inline void northstarcpp::strings::stream::clear			( )
{
	m_buffer_size	= 0;
}

#endif // #ifndef NORTHSTARCPP_STRINGS_STREAM_INLINE_H_INCLUDED