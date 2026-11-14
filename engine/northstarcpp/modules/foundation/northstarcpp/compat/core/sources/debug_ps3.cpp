////////////////////////////////////////////////////////////////////////////
//	Created		: 14.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/os_include.h>
#include "debug_platform.h"
#include "debug.h"

#pragma comment( lib, "c")			// for _Sce_assert
#pragma comment( lib, "cs")			// for _Sce_assert
#pragma comment( lib, "c_stub")		// for _Sce_assert
#pragma comment( lib, "stdc++")	// for _Sce_assert

bool northstarcpp::debug::is_debugger_present( )
{
#ifdef DEBUG
	return						( !!IS_DEBUGGER_PRESENT() );
#else // #ifdef DEBUG
	return						( false );
#endif // #ifdef DEBUG
}

pcstr northstarcpp::core::debug::platform::get_graphics_api_error_description	( int const error_code )
{
	NOT_IMPLEMENTED				( return "" );
}

void northstarcpp::core::debug::notify_xbox_debugger ( pcstr message )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( message );
}