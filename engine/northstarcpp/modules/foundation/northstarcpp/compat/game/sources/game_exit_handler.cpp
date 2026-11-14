////////////////////////////////////////////////////////////////////////////
//	Created		: 03.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "game_exit_handler.h"
#include "game.h"
#include <northstarcpp/compat/input/world.h>
#include <northstarcpp/compat/input/gamepad.h>
#include <northstarcpp/compat/input/keyboard.h>
#include <northstarcpp/compat/input/mouse.h>

using stalker2::exit_handler;

exit_handler::exit_handler	(  stalker2::game& game )
:m_game( game )
{
}

exit_handler::~exit_handler	( )
{
}

bool exit_handler::on_keyboard_action	( northstarcpp::input::world& input_world, northstarcpp::input::enum_keyboard key, northstarcpp::input::enum_keyboard_action action )
{
	if(action!=northstarcpp::input::kb_key_down)
		return false;

	if ( key == northstarcpp::input::key_f4 )
		if( input_world.get_keyboard()->is_key_down( northstarcpp::input::key_lalt ) || 
			input_world.get_keyboard()->is_key_down( northstarcpp::input::key_ralt )	) 
	{
		m_game.engine().exit	( 0 );
		return					true;
	}

	if ( key == northstarcpp::input::key_grave )
	{
		m_game.toggle_console	( );
		return					true;
	}
	
	return		false;
}

bool exit_handler::on_gamepad_action	( northstarcpp::input::world& input_world, northstarcpp::input::gamepad_button button, northstarcpp::input::enum_gamepad_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS		(action, button, &input_world);
	return false;
}

bool exit_handler::on_mouse_key_action	( northstarcpp::input::world& input_world, northstarcpp::input::mouse_button button, northstarcpp::input::enum_mouse_key_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS		(action, button, &input_world);
	return false;
}

bool exit_handler::on_mouse_move		( northstarcpp::input::world& input_world, int x, int y, int z )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS		(&input_world, x, y, z);
	return false;
}
