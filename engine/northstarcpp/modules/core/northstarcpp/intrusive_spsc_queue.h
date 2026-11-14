////////////////////////////////////////////////////////////////////////////
//	Created		: 14.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INTRUSIVE_SPSC_QUEUE_H_INCLUDED
#define INTRUSIVE_SPSC_QUEUE_H_INCLUDED

namespace northstarcpp {

template < typename T, typename BaseWithMember, T* BaseWithMember::*MemberNext, typename head_type = T* >
class intrusive_spsc_queue {
public:
	inline			intrusive_spsc_queue	( );
	inline			~intrusive_spsc_queue	( );

	inline	void	push_back				( T* const value );
	inline	T*		pop_front				( T*& item_to_delete );
	inline	bool	empty					( ) const;

	inline	void	set_push_thread_id		( );
	inline	void	push_null_node			( T* const null_node );

	inline	void	set_pop_thread_id		( );
	inline	T*		pop_null_node			( );

protected:
	head_type		m_head;
	threading::atomic32_type	m_push_thread_id;
	threading::atomic32_type	m_pop_thread_id;
	char			m_cache_line_pad[NORTHSTARCPP_MAX_CACHE_LINE_SIZE - 1*sizeof(T*) -2*sizeof(threading::atomic32_type)];
	T*				m_tail;
}; // class intrusive_spsc_queue

} // namespace northstarcpp

#include <northstarcpp/intrusive_spsc_queue_inline.h>

#endif // #ifndef INTRUSIVE_SPSC_QUEUE_H_INCLUDED