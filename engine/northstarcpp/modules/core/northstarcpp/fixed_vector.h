////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_FIXED_VECTOR_H_INCLUDED
#define NORTHSTARCPP_FIXED_VECTOR_H_INCLUDED

namespace northstarcpp {

template < typename T, int max_count >
class fixed_vector : public buffer_vector< T > {
private:
	typedef fixed_vector<T, max_count>	self_type;
	typedef buffer_vector<T>			super;
	typedef typename super::size_type	size_type;
	typedef typename super::value_type	value_type;

public:
	inline				fixed_vector	();
	inline				fixed_vector	(size_type count, value_type const &value);
	inline	IMPLICIT	fixed_vector	(self_type const &other);
	template <typename input_iterator>
	inline				fixed_vector	(input_iterator const& first, input_iterator const& last);

	inline	self_type&	operator=		(self_type const &other);

private:
	struct allign_helper {
		char	m_store[sizeof(T)];
	}; // struct allign_helper

	allign_helper		m_buffer[max_count];
};

} // namespace northstarcpp

	template < typename T, int max_count >
	inline	void		swap			( northstarcpp::fixed_vector< T, max_count >& left, northstarcpp::fixed_vector< T, max_count >& right);

#include <northstarcpp/fixed_vector_inline.h>

#endif // #ifndef NORTHSTARCPP_FIXED_VECTOR_H_INCLUDED