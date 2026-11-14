////////////////////////////////////////////////////////////////////////////
//	Created		: 24.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "scene.h"
#include "game.h"
#include <northstarcpp/input/world.h>
#include <northstarcpp/input/keyboard.h>
#include <northstarcpp/input/mouse.h>
#include <northstarcpp/input/gamepad.h>

namespace stalker2 {

scene::scene( game& g )
:m_game( g )
{
}

void scene::on_activate( )
{
	m_game.input_world().add_handler( *this );
}

void scene::on_deactivate( )
{
	m_game.input_world().remove_handler( *this );
}

bool scene::on_keyboard_action( northstarcpp::input::world& input_world, northstarcpp::input::enum_keyboard key, northstarcpp::input::enum_keyboard_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &input_world, key, action );
	return false;
}

bool scene::on_gamepad_action( northstarcpp::input::world& input_world, northstarcpp::input::gamepad_button button, northstarcpp::input::enum_gamepad_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &input_world, button, action );
	return false;
}

bool scene::on_mouse_key_action( northstarcpp::input::world& input_world, northstarcpp::input::mouse_button button, northstarcpp::input::enum_mouse_key_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &input_world, button, action );
	return false;
}

bool scene::on_mouse_move( northstarcpp::input::world& input_world, int x, int y, int z )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &input_world, x, y, z );
	return false;
}

} // namespace stalker2
