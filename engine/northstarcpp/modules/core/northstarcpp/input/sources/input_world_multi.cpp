////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "input_world.h"
#include "receiver_gamepad.h"

using northstarcpp::input::input_world;

void input_world::create_devices	( HWND const window_handle )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( window_handle );

	m_gamepad			= NEW ( receiver::gamepad ) ( 0, *this );
}

void input_world::destroy_devices	( )
{
	NORTHSTARCPP_DELETE_IMPL	( *g_allocator, m_gamepad );
}