////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LOOSE_PTR_BASE_INLINE_H_INCLUDED
#define NORTHSTARCPP_LOOSE_PTR_BASE_INLINE_H_INCLUDED

inline northstarcpp::loose_ptr_base::loose_ptr_base		( )
{
	m_pointer								= MT_NEW( loose_ptr_data ) ( this );
	++m_pointer->m_reference_count;
}

inline northstarcpp::loose_ptr_base::~loose_ptr_base	( )
{
	ASSERT									( m_pointer );
	if ( m_pointer->m_reference_count > 1 ) {
		LOGI_WARNING						( "debug", "dangling pointer found(%d): 0x%08x", m_pointer->m_reference_count - 1, this );
		northstarcpp::debug::dump_call_stack		( "debug", northstarcpp::logging::warning, 2, 0, 0 );
	}

	--m_pointer->m_reference_count;
	m_pointer->m_pointer					= 0;
}

#endif // #ifndef NORTHSTARCPP_LOOSE_PTR_BASE_INLINE_H_INCLUDED