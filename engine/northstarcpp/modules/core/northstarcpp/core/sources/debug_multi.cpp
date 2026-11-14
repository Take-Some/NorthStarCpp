////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_platform.h"
#include "debug.h"

#pragma message( NORTHSTARCPP_TODO("PS3: core dump generation") )
#pragma message( NORTHSTARCPP_TODO("PS3: save debug data") )

bool northstarcpp::core::debug::platform::error_after_dialog	( )
{
	return						( false );
}

void northstarcpp::debug::dump_call_stack					( pcstr						initiator, 
													  logging::verbosity const	verbosity, 
													  u32 						num_first_to_ignore, 
													  u32 						num_last_to_ignore, 
													  _EXCEPTION_POINTERS*		pointers, 
													  void*						callstack[] )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( initiator, verbosity, num_first_to_ignore, num_last_to_ignore, pointers, callstack );
}

void northstarcpp::core::debug::platform::on_error				( bool* do_debug_break, char* const message, u32 message_size, bool* ignore_always, _EXCEPTION_POINTERS* const exception_information, error_type_enum error_type )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( do_debug_break, ignore_always, exception_information, error_type, message_size );
	northstarcpp::core::debug::on_error	( message );
}

void northstarcpp::core::debug::platform::on_error_message_box	( bool* do_debug_break, char* const message, u32 message_size, bool* ignore_always, _EXCEPTION_POINTERS* const exception_information, error_type_enum error_type )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( do_debug_break, ignore_always, exception_information, error_type, message_size );
	northstarcpp::core::debug::on_error	( message );
}

pstr northstarcpp::core::debug::platform::fill_format_message	( int const error_code )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( error_code );
	return						( "" );
}

void northstarcpp::core::debug::platform::free_format_message	( pstr const buffer )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( buffer );
}

void northstarcpp::core::debug::platform::terminate				( pcstr message, int const error_code )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( message );
	::exit						( error_code );
}