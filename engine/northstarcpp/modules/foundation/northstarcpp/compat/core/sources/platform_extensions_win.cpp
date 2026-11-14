////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "platform_extensions_win.h"
#include <northstarcpp/compat/os_include.h>
#include "delayimp.h"

#pragma comment(lib, "delayimp")



bool northstarcpp::platform::unload_delay_loaded_library	( pcstr dll_name )
{
	return	( ( __FUnloadDelayLoadedDLL2(dll_name) != FALSE ) || true );
}