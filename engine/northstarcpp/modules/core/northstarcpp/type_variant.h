////////////////////////////////////////////////////////////////////////////
//	Created		: 26.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_TYPE_VARIANT_H_INCLUDED
#define NORTHSTARCPP_TYPE_VARIANT_H_INCLUDED

namespace northstarcpp {

namespace detail {
class abstract_type_helper;
} // namespace detail

#pragma warning(push)
#pragma warning(disable:4324)

template <int MaxSize>
class variant
{
public:
			variant		();
			variant		(variant const & other);
		   ~variant		();

	template <class T>
	void	set			(T const & value);

	template <class T>
	bool	try_get		(T & out_value);

	void	operator =	(variant const & other);

private:
	void	destroy_previous_variable_if_needed	();

private:
	char	NORTHSTARCPP_DEFAULT_ALIGN			m_helper_storage[sizeof(pvoid)];
	char	NORTHSTARCPP_DEFAULT_ALIGN			m_storage[MaxSize];
	detail::abstract_type_helper *		m_helper;
	u32									m_type_id;
};

#pragma warning(default:4324)


} // namespace northstarcpp

#include <northstarcpp/type_variant_inline.h>

#endif // #ifndef NORTHSTARCPP_TYPE_VARIANT_H_INCLUDED