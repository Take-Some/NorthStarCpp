////////////////////////////////////////////////////////////////////////////
//	Created 	: 02.03.2009
//	Author		: Kayla Verner, Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INTRUSIVE_LIST_H_INCLUDED
#define NORTHSTARCPP_INTRUSIVE_LIST_H_INCLUDED

#include <northstarcpp/threading_policies.h>

namespace northstarcpp {

template <typename T, typename BaseWithMember, T* BaseWithMember::*MemberNext, typename Policy = threading::multi_threading_mutex_policy>
class intrusive_list : private detail::noncopyable {
public:
	typedef				Policy				policy;
	typedef				T					data_type;

public:
	inline				intrusive_list		();

	inline	void		swap				(intrusive_list& other);
	inline	void		clear				();

	inline	void		push_back			(T* object, bool * out_pushed_first = NULL);
	inline	void		push_front			(T* object, bool * out_pushed_first = NULL);

	inline	bool		push_back_unique	(T* object, bool * out_pushed_first = NULL);
	inline	bool		push_front_unique	(T* object, bool * out_pushed_first = NULL);

	inline	T*			pop_front			();
	inline	T*			pop_front			(bool * out_popped_last);
	inline	T*			pop_all_and_clear	();
	inline	T*			front				() const;
	inline	T*			back				() const;
	inline	bool		empty				() const;

	inline	bool		erase				(T* object);

	template <class Predicate>
	inline	void		for_each			(Predicate const & pred);
	template <class Predicate>
	inline	void		for_each			(Predicate & pred);
	template <class Predicate>
	inline	bool		erase_if			(Predicate & pred);

	static T*			get_next_of_object	(T const * object) { return object->*MemberNext; }
	static void			set_next_of_object	(T * object, T * next) { object->*MemberNext = next; }

	Policy &			threading_policy	() const { return m_threading_policy; }
private:
	inline	bool		contains_object_thread_unsafe	(T* object);

	template <class Predicate>
	class predicate_ref :
		private boost::noncopyable
	{
	public:
			predicate_ref	(Predicate & pred) : m_predicate_ref(pred) {}
	void	operator()		(T* arg) const { m_predicate_ref(arg); }
	private:
		Predicate &	m_predicate_ref;
	}; // class predicate_ref

	T* 					m_first;
	T*					m_last;
	mutable Policy		m_threading_policy;
};

} // namespace northstarcpp

template <typename T, typename BaseWithMember, T* BaseWithMember::*MemberNext, typename Policy>
inline	void			swap			( northstarcpp::intrusive_list<T, BaseWithMember, MemberNext>& left, 
										  northstarcpp::intrusive_list<T, BaseWithMember, MemberNext>& right );

#include <northstarcpp/intrusive_list_inline.h>

#endif // #ifndef NORTHSTARCPP_INTRUSIVE_LIST_H_INCLUDED