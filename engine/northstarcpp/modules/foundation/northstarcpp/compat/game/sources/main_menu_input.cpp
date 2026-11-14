////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "main_menu.h"
#include "game.h"
#include <northstarcpp/compat/input/keyboard.h>
#include <northstarcpp/compat/input/mouse.h>
#include <northstarcpp/compat/input/gamepad.h>

namespace stalker2{

bool main_menu::on_keyboard_action( input::world& input_world, input::enum_keyboard key, input::enum_keyboard_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( input_world );
	if(action==northstarcpp::input::kb_key_down)
	{
		if(key == northstarcpp::input::key_escape)
		{
			m_game.main_menu_close_query();
			return					true;
		}

		if(key == northstarcpp::input::key_q)
		{
			m_game.exit			( "quit" );
			return				true;
		}
	}
	return dialog_input_handler()->on_keyboard_action( input_world, key, action );
}

bool main_menu::on_gamepad_action( input::world& input_world, input::gamepad_button button, input::enum_gamepad_action action )
{
	return dialog_input_handler()->on_gamepad_action( input_world, button, action );
}

bool main_menu::on_mouse_key_action( input::world& input_world, input::mouse_button button, input::enum_mouse_key_action action )
{
	return dialog_input_handler()->on_mouse_key_action( input_world, button, action );
}

bool main_menu::on_mouse_move( input::world& input_world, int x, int y, int z )
{
	return dialog_input_handler()->on_mouse_move( input_world, x, y, z );
}

} // namespace stalker2

