////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DEBUG_H_INCLUDED
#define NORTHSTARCPP_DEBUG_H_INCLUDED

#ifdef __GCC__
#	include <northstarcpp/compat/core/core.h>
#endif // #ifdef __GCC__

struct _exception;
struct _EXCEPTION_POINTERS;

namespace northstarcpp {

namespace core {
namespace debug {
	enum error_mode;
	enum bugtrap_usage;
} // namespace debug
} // namespace core

enum assert_enum;
enum process_error_enum;

namespace logging {
	enum verbosity;
} // namespace logging

namespace debug {

NORTHSTARCPP_CORE_API	void __cdecl	on_error			( bool* do_debug_break, process_error_enum process_error, bool* ignore_always, assert_enum assert_type, pcstr reason, pcstr expression, pcstr file, pcstr function, u32 line, pcstr format, ... );
NORTHSTARCPP_CORE_API	void			on_error			( bool* do_debug_break, process_error_enum process_error, bool* ignore_always, assert_enum assert_type, pcstr reason, pcstr expression, pcstr file, pcstr function, u32 line );

NORTHSTARCPP_CORE_API	void __cdecl	on_error			( bool* do_debug_break, process_error_enum process_error, bool* ignore_always, int error_code, pcstr expression, pcstr file, pcstr function, u32 line, pcstr format, ... );
NORTHSTARCPP_CORE_API	void			on_error			( bool* do_debug_break, process_error_enum process_error, bool* ignore_always, int error_code, pcstr expression, pcstr file, pcstr function, u32 line );

NORTHSTARCPP_CORE_API	void __cdecl	terminate			( pcstr format, ... );

NORTHSTARCPP_CORE_API	void			output				( pcstr message );
NORTHSTARCPP_CORE_API	void __cdecl	printf				( pcstr format, ... );

NORTHSTARCPP_CORE_API	void			change_bugtrap_usage( core::debug::error_mode, core::debug::bugtrap_usage );

NORTHSTARCPP_CORE_API	void			dump_call_stack		( pcstr						initiator, 
													  logging::verbosity const	verbosity, 
													  u32 						num_first_to_ignore, 
													  u32 						num_last_to_ignore, 
													  _EXCEPTION_POINTERS*		pointers, 
													  void*						callstack[] = 0);

NORTHSTARCPP_CORE_API	void			log_call_stack		( pcstr header );

typedef void protected_function_type				( pvoid );
NORTHSTARCPP_CORE_API	void			protected_call		( protected_function_type* function_to_call, pvoid arguments );

NORTHSTARCPP_CORE_API	int				on_math_error		( struct ::_exception* exception );

NORTHSTARCPP_CORE_API	bool			is_debugger_present	( );

NORTHSTARCPP_CORE_API	int		unhandled_exception_filter	( int const exception_code, 
													 _EXCEPTION_POINTERS* const exception_information );



#ifdef DEBUG
NORTHSTARCPP_CORE_API	bool			is_fpe_enabled		( );
NORTHSTARCPP_CORE_API	void			enable_fpe			( bool value );
#endif // #ifdef DEBUG

} // namespace debug
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_DEBUG_H_INCLUDED