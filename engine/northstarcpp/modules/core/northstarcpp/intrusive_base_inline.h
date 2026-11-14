////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INTRUSIVE_BASE_INLINE_H_INCLUDED
#define NORTHSTARCPP_INTRUSIVE_BASE_INLINE_H_INCLUDED

inline northstarcpp::intrusive_base::intrusive_base							( ) :
	m_reference_count	( 0 )
{
}

template <typename T>
inline void northstarcpp::intrusive_base::destroy							( T* object ) const
{
	MT_DELETE			( object );
}

inline northstarcpp::editor_intrusive_base::editor_intrusive_base	( ) :
	m_reference_count	( 0 )
{
}

template <typename T>
inline void northstarcpp::editor_intrusive_base::destroy						( T* object ) const
{
	NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::editor::g_allocator, object );
}

inline northstarcpp::intrusive_interlocked_base::intrusive_interlocked_base	( ) :
	m_reference_count	( 0 )
{
}

template <typename T>
inline void northstarcpp::intrusive_interlocked_base::destroy				( T* object ) const
{
	MT_DELETE			( object );
}

#endif // #ifndef NORTHSTARCPP_INTRUSIVE_BASE_INLINE_H_INCLUDED