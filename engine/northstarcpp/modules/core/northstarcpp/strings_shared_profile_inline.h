////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_STRINGS_SHARED_PROFILE_INLINE_H_INCLUDED
#define NORTHSTARCPP_STRINGS_SHARED_PROFILE_INLINE_H_INCLUDED

inline northstarcpp::threading::atomic32_type const& northstarcpp::strings::shared::profile::reference_count	( ) const
{
	return				( m_reference_count );
}

inline const u32 &northstarcpp::strings::shared::profile::checksum			( ) const
{
	ASSERT				( m_checksum == create_checksum ( value(), value() + m_length ), "shared string is corrupted" );
	return				( m_checksum );
}

inline const u32 &northstarcpp::strings::shared::profile::checksum_no_check	( ) const
{
	return				(m_checksum);
}

inline u32 const& northstarcpp::strings::shared::profile::length			( ) const
{
	ASSERT				( strings::length( value( ) ) == m_length, "shared string is corrupted" );
	return				( m_length );
}

inline u32 const& northstarcpp::strings::shared::profile::length_no_check	( ) const
{
	return				( m_length );
}

inline pcstr northstarcpp::strings::shared::profile::value					( ) const
{
	return				( ( ( pcstr )this ) + sizeof( profile ) );
}

inline northstarcpp::strings::shared::profile* northstarcpp::strings::shared::profile::get_next_hash_node	( ) const
{
	return				( m_next );
}

inline void northstarcpp::strings::shared::profile::set_next_hash_node		( profile* const value )
{
	m_next				= value;
}

#endif // #ifndef NORTHSTARCPP_STRINGS_SHARED_PROFILE_INLINE_H_INCLUDED