////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_SHARED_STRING_H_INCLUDED
#define NORTHSTARCPP_SHARED_STRING_H_INCLUDED

#include <northstarcpp/strings_shared_profile.h>

template class NORTHSTARCPP_CORE_API northstarcpp::intrusive_ptr<
	northstarcpp::strings::shared::profile,
	northstarcpp::strings::shared::detail::intrusive_base,
	northstarcpp::threading::multi_threading_mutex_policy
>;

namespace northstarcpp {

class NORTHSTARCPP_CORE_API shared_string {
public:
	inline				shared_string						( );
			IMPLICIT	shared_string						( pcstr value );
	inline	IMPLICIT	shared_string						( shared_string const& object );

public:
	inline	pcstr		c_str								( ) const;
	inline	bool		operator!							( ) const;
	inline	bool		operator<							( shared_string const& string ) const;
	inline	bool		operator==							( shared_string const& string ) const;
	inline	bool		operator!=							( shared_string const& string ) const;
	inline	u32			length								( ) const;
	inline	u32			size								( ) const;

public:
	typedef pcstr ( shared_string::*unspecified_bool_type )	( ) const;
	inline				operator unspecified_bool_type		( ) const;

private:
	typedef strings::shared::profile::profile_ptr			profile_ptr;

private:
	profile_ptr			m_pointer;
}; // class shared_string

} // namespace northstarcpp

#include <northstarcpp/shared_string_inline.h>

#endif // #ifndef NORTHSTARCPP_SHARED_STRING_H_INCLUDED