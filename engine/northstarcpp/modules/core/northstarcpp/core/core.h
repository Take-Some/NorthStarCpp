////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CORE_CORE_H_INCLUDED
#define NORTHSTARCPP_CORE_CORE_H_INCLUDED

namespace northstarcpp {
namespace core {

struct engine;

namespace debug {

enum error_mode {
	error_mode_silent,
	error_mode_verbose,
}; // enum error_mode

enum bugtrap_usage {
	no_bugtrap,
	native_bugtrap,
	managed_bugtrap,
}; // enum error_mode
} // namespace debug

enum log_file_usage {
	create_log,
	append_log,
	no_log,
}; // enum error_mode

enum debug_initialization {
	delay_debug_initialization,
	perform_debug_initialization,
}; // enum bugtrap_usage

NORTHSTARCPP_CORE_API	void	preinitialize	(
							core::engine * engine,
							pcstr command_line,
							command_line::contains_application_bool,
							pcstr application,
							pcstr build_date
						);
NORTHSTARCPP_CORE_API	void	initialize	(
							pcstr debug_thread_id,
							log_file_usage log_file_usage,
							debug_initialization debug_initialization
						);
NORTHSTARCPP_CORE_API	void	finalize	( );

				bool	initialized ( );

} // namespace core

} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CORE_CORE_H_INCLUDED