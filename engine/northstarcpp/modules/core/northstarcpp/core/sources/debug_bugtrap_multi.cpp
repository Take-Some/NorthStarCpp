////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_bugtrap.h"
#include "debug.h"
#include <northstarcpp/core/core.h>

bool northstarcpp::core::debug::bugtrap::initialized	( )
{
	return		( true );
}

void northstarcpp::core::debug::bugtrap::initialize		( )
{
}

void northstarcpp::core::debug::bugtrap::finalize		( )
{
}

bool northstarcpp::core::debug::bugtrap::on_thread_spawn( )
{
	return		( true );
}

void northstarcpp::core::debug::bugtrap::add_file		( pcstr full_path_file_name )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( full_path_file_name );
}

void northstarcpp::core::debug::bugtrap::change_usage	( error_mode error_mode, bugtrap_usage bugtrap_usage )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( error_mode, bugtrap_usage );
}

void northstarcpp::core::debug::on_error				( pcstr message )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( message );
}