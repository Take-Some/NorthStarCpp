////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef LOGGING_H_INCLUDED
#define LOGGING_H_INCLUDED

#include <fastdelegate/fastdelegate.h>
#include <northstarcpp/core/core.h>

namespace northstarcpp {
namespace logging {

	enum verbosity_recursion {
		recurse_all	=	0,
		recurse_0	=	256,
	}; // enum recurse_level

	void			preinitialize			( );
	void			initialize				( core::log_file_usage log_file_usage, int root_rule );
	void			finalize				( );

	pcstr			verbosity_to_string		( int verbosity );
	verbosity		string_to_verbosity 	( pcstr in_verbosity );

	bool			ready_for_use			( );
	void			ready_for_use			( bool value );

	pcstr			log_file_name			( );

	bool			initialize_console		( );
	void			initialize_stdout_if_needed ( );
	void			finalize_console		( );
	bool			use_console_for_logging ( );
	void			write_to_stdout			( pcstr format, ... );

	void			write_exit_code_file	( int error_code );

	threading::mutex&	get_log_mutex		( );
	void			try_lock_log_mutex		( );
	void			try_unlock_log_mutex	( );

	typedef memory::doug_lea_allocator_type	allocator_type;
	extern allocator_type					g_allocator;

#	define USER_ALLOCATOR					::northstarcpp::logging::g_allocator
#	include <northstarcpp/std_containers.h>
#	undef USER_ALLOCATOR

} // namespace logging
} // namespace northstarcpp

#define LOG_NEW( type )								NORTHSTARCPP_NEW_IMPL(		::northstarcpp::logging::g_allocator, type )
#define LOG_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::logging::g_allocator, pointer )
#define LOG_MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	::northstarcpp::logging::g_allocator, size, description )
#define LOG_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	::northstarcpp::logging::g_allocator, pointer, size, description )
#define LOG_FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		::northstarcpp::logging::g_allocator, pointer )
#define LOG_ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::logging::g_allocator, type, count )

#endif // #ifndef LOGGING_H_INCLUDED