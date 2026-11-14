////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LOOSE_PTR_DATA_INLINE_H_INCLUDED
#define NORTHSTARCPP_LOOSE_PTR_DATA_INLINE_H_INCLUDED

inline northstarcpp::loose_ptr_data::loose_ptr_data	( loose_ptr_base* const pointer ) :
	m_pointer	( pointer )
{
	ASSERT		( pointer );
}

template < typename T >
inline void northstarcpp::loose_ptr_data::destroy	( T* object ) const
{
	MT_DELETE	( object );
}

#endif // #ifndef NORTHSTARCPP_LOOSE_PTR_DATA_INLINE_H_INCLUDED