////////////////////////////////////////////////////////////////////////////
//	Created 	: 21.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "receiver_keyboard.h"
#include <northstarcpp/input/handler.h>

using northstarcpp::input::receiver::keyboard;
using northstarcpp::input::handler;
using northstarcpp::input::enum_keyboard;
using northstarcpp::input::world;

keyboard::keyboard			( IDirectInput8A& direct_input, HWND const window_handle, world& input_world ) :
	m_window_handle				( window_handle ),
	m_device					( 0 ),
	m_world						( input_world )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( direct_input );
}

keyboard::~keyboard			( )
{
}

void keyboard::on_activate	( )
{
}

void keyboard::on_deactivate( )
{
}

bool keyboard::is_key_down	( char const value ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( value );
	return						( false );
}

bool keyboard::is_key_down	( enum_keyboard const key ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( key );
	return						( false );
}

void keyboard::execute		( )
{
}