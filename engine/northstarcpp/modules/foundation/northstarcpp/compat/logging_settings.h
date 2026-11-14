////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef LOGGING_SETTINGS_H_INCLUDED
#define LOGGING_SETTINGS_H_INCLUDED

namespace northstarcpp {
namespace logging {

class NORTHSTARCPP_CORE_API settings : private detail::noncopyable {
public:
	enum			flags_enum { flags_log_only_user_string	=	1, 
								 flags_log_to_console		=	2, };
	flags_enum		flags;

	explicit		settings			( pcstr initiator_prefix, u32 flags = 0 );
	inline	pcstr	get_initiator_prefix() const { return m_initiator_prefix; }

private:
					settings			();
	string256		m_initiator_prefix;
}; // class settings

} // namespace logging
} // namespace northstarcpp

#endif // #ifndef LOGGING_SETTINGS_H_INCLUDED