////////////////////////////////////////////////////////////////////////////
//	Created		: 17.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_STREAM_INLINE_H_INCLUDED
#define NORTHSTARCPP_MEMORY_STREAM_INLINE_H_INCLUDED

inline northstarcpp::memory::stream::stream				( memory::base_allocator* allocator ) :
	m_allocator		( allocator ),
	m_buffer		( 0 ),
	m_buffer_size	( 0 ),
	m_allocated_size( 0 )
{
}

inline northstarcpp::memory::stream::~stream			( )
{
	NORTHSTARCPP_FREE_IMPL	( *m_allocator, m_buffer );
}

inline void northstarcpp::memory::stream::append		( u32 const value )
{
	append			( &value, sizeof(value) );
}

inline void northstarcpp::memory::stream::append		( int const value )
{
	append			( &value, sizeof(value) );
}

inline void northstarcpp::memory::stream::append		( pcstr const value )
{
	append			( value, (strings::length(value) + 1)*sizeof(char) );
}

inline pbyte northstarcpp::memory::stream::get_buffer	( ) const
{
	return			m_buffer;
}

inline u32 northstarcpp::memory::stream::get_buffer_size( ) const
{
	return			m_buffer_size;
}

inline void northstarcpp::memory::stream::clear			( )
{
	m_buffer_size	= 0;
}

#endif // #ifndef NORTHSTARCPP_MEMORY_STREAM_INLINE_H_INCLUDED