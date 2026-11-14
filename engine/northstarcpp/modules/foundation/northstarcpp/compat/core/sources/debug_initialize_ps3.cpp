////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/core/core.h>		// for error_mode
#include "debug_platform.h"
#include <northstarcpp/compat/debug.h>
#include "debug.h"
#include <sys/dbg.h>			// for floating point exceptions

#pragma comment( lib, "lv2dbg_stub" )

namespace northstarcpp {
namespace core {
namespace debug {
	extern northstarcpp::command_line::key	g_fpe_disabled;
} // namespace debug 
} // namespace core
} // namespace northstarcpp

void northstarcpp::core::debug::platform::change_storage_access_handler	(
		error_mode error_mode, 
		void (*storage_access_handler) ( int )
	)
{
	NOT_IMPLEMENTED	( );
}

namespace northstarcpp {
namespace core {
namespace debug {

static bool s_debug_preinitialized		=	false;

void preinitialize						( )
{
	if ( s_debug_preinitialized )
		return;

	s_debug_preinitialized				= true;
}

} // namespace debug
} // namespace core
} // namespace northstarcpp

#ifdef DEBUG
void northstarcpp::debug::enable_fpe			( bool value )
{
	if ( value )
		sys_dbg_enable_floating_point_enabled_exception	( threading::current_thread_id(), 0, 0, 0 );
	else
		sys_dbg_disable_floating_point_enabled_exception( threading::current_thread_id(), 0, 0, 0 );
}
#endif // #ifdef DEBUG

void northstarcpp::core::debug::on_thread_spawn	( )
{
#if defined(DEBUG)
	if ( !g_fpe_disabled )
		northstarcpp::debug::enable_fpe		( true );
#endif // #if defined(DEBUG)
}