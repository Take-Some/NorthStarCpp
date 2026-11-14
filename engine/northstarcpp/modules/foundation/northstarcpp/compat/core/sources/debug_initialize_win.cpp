////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_platform.h"

void northstarcpp::core::debug::platform::setup_storage_access_handler	( error_mode error_mode, 
																  void (*storage_access_handler) ( int ) )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER			( storage_access_handler );
	setup_unhandled_exception_handler	( error_mode );
}

void northstarcpp::core::debug::platform::change_storage_access_handler	( error_mode error_mode, 
																  void (*storage_access_handler) ( int ) )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER			( storage_access_handler );
	change_unhandled_exception_handler	( error_mode );
}
