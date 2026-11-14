////////////////////////////////////////////////////////////////////////////
//	Created 	: 08.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INTRUSIVE_LIST_FAST_H_INCLUDED
#define NORTHSTARCPP_INTRUSIVE_LIST_FAST_H_INCLUDED

#include <northstarcpp/compat/threading_policies.h>

namespace northstarcpp {

template <typename T, typename BaseWithMember, T* BaseWithMember::*MemberNext, typename Policy = threading::multi_threading_mutex_policy>
class intrusive_queue : private detail::noncopyable {
public:
	typedef				Policy				policy;
	typedef				T					data_type;

public:
	inline				intrusive_queue	();

	inline	void		push_back			(T* object);

	inline	T*			pop_front			();
	inline	T*			front				();
	inline	bool		empty				() const;

	static T*			get_next_of_object	(T const * object) { return object->*MemberNext; }

	Policy &			threading_policy	() const { return m_threading_policy; }

private:
	T * 				m_pop_list;
	char				m_cache_line_pad	[NORTHSTARCPP_MAX_CACHE_LINE_SIZE - sizeof(T*) ];
	T *					m_push_list;

	mutable Policy		m_threading_policy;
};

} // namespace northstarcpp

#include <northstarcpp/compat/intrusive_queue_inline.h>

#endif // #ifndef NORTHSTARCPP_INTRUSIVE_LIST_FAST_H_INCLUDED