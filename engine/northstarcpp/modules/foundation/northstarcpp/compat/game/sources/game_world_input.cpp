////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "game_world.h"
#include "game.h"
#include <northstarcpp/compat/input/world.h>
#include <northstarcpp/compat/input/keyboard.h>

namespace stalker2{

bool game_world::on_keyboard_action( input::world& input_world, input::enum_keyboard key, input::enum_keyboard_action action )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( input_world );

	if(key == northstarcpp::input::key_escape && action==northstarcpp::input::kb_key_down)
	{
		m_game.game_world_close_query	( );
		return					true;
	}

	return false;
}

} // namespace stalker2