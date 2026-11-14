////////////////////////////////////////////////////////////////////////////
//	Created		: 27.10.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_platform.h"
#include "debug.h"
#include <northstarcpp/compat/os_include.h>
#include <dxerr9.h>
#include <northstarcpp/compat/fixed_string.h>
#include <northstarcpp/compat/core/engine.h>

#pragma comment( lib, "dxerr9.lib" )

pcstr northstarcpp::core::debug::platform::get_graphics_api_error_description	( int const error_code )
{
	return						DXGetErrorDescription9( error_code );
}

bool northstarcpp::debug::is_debugger_present( )
{
#ifdef DEBUG
	return						( !!IS_DEBUGGER_PRESENT() );
#else // #ifdef DEBUG
	return						( false );
#endif // #ifdef DEBUG
}

#ifdef DEBUG
void northstarcpp::core::debug::notify_xbox_debugger ( pcstr in_message )
{
	DmSendNotificationString	(in_message);
}
#else // #ifdef DEBUG
void northstarcpp::core::debug::notify_xbox_debugger ( pcstr in_message )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	(in_message);
}
#endif // #ifdef DEBUG