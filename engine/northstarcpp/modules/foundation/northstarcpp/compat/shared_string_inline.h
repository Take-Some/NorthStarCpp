////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_SHARED_STRING_INLINE_H_INCLUDED
#define NORTHSTARCPP_SHARED_STRING_INLINE_H_INCLUDED

inline northstarcpp::shared_string::shared_string					( )
{
}

inline northstarcpp::shared_string::shared_string					( shared_string const& other ) :
	m_pointer	( other.m_pointer )
{
}

inline pcstr northstarcpp::shared_string::c_str						( ) const
{
	if ( m_pointer )
		return	( m_pointer->value( ) );

	return		( 0 );
}

inline bool northstarcpp::shared_string::operator!					( ) const
{
	return		( !m_pointer );
}

inline bool northstarcpp::shared_string::operator<					( shared_string const& other ) const
{
	return		( m_pointer < other.m_pointer );
}

inline bool northstarcpp::shared_string::operator==					( shared_string const& other ) const
{
	return		( m_pointer == other.m_pointer );
}

inline bool northstarcpp::shared_string::operator!=					( shared_string const& other ) const
{
	return		( m_pointer != other.m_pointer );
}

inline u32 northstarcpp::shared_string::length						( ) const
{
	R_ASSERT	( m_pointer );
	return		( m_pointer->length() );
}

inline u32 northstarcpp::shared_string::size						( ) const
{
	if ( !m_pointer )
		return	( 0 );

	return		( length( ) );
}

inline northstarcpp::shared_string::operator unspecified_bool_type	( ) const
{
	if ( !m_pointer )
		return	( 0 );

	return		( &shared_string::c_str );
}

#endif // #ifndef NORTHSTARCPP_SHARED_STRING_INLINE_H_INCLUDED