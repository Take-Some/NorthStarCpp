////////////////////////////////////////////////////////////////////////////
//	Created		: 17.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INTRUSIVE_SPMC_QUEUE_INLINE_H_INCLUDED
#define INTRUSIVE_SPMC_QUEUE_INLINE_H_INCLUDED

//namespace northstarcpp {
//
//template < typename T, typename BaseWithMember, T* BaseWithMember::*MemberNext >
//inline T* intrusive_spmc_queue<T,BaseWithMember,MemberNext>::pop_front	( T*& item_to_delete )
//{
//	R_ASSERT_CMP			( m_head, !=, 0, "do not call pop_front after pop_null_node called" );
//	R_ASSERT_CMP			( m_tail, !=, 0, "unexpected situation" );
//	R_ASSERT_CMP			( m_pop_thread_id, ==, (long)threading::current_thread_id() );
//	do {
//		item_to_delete		= m_tail;
//		T* const value		= item_to_delete->*MemberNext;
//		if ( !value )
//			return			0;
//	} while ( threading::interlocked_compare_exchange_pointer( m_tail, item_to_delete->*MemberNext, item_to_delete) != item_to_delete );
//	return					item_to_delete->*MemberNext;
//}
//
//} // namespace northstarcpp

#endif // #ifndef INTRUSIVE_SPMC_QUEUE_INLINE_H_INCLUDED