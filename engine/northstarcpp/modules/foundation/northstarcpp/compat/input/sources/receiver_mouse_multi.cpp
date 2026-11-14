////////////////////////////////////////////////////////////////////////////
//	Created 	: 21.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "receiver_mouse.h"
#include <northstarcpp/compat/input/handler.h>

using northstarcpp::input::receiver::mouse;
using northstarcpp::input::handler;
using northstarcpp::input::world;

mouse::mouse			( IDirectInput8A& direct_input, HWND const window_handle, world& input_world ) 
:	m_window_handle		( window_handle ),
	m_device			( 0 ),
	m_world				( input_world )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( direct_input );
}

mouse::~mouse			( )
{
}

northstarcpp::input::mouse::state const& mouse::get_state	( ) const
{
	return				( m_current_state );
}

void mouse::on_activate	( )
{
}

void mouse::on_deactivate ( )
{
}

void mouse::execute		( )
{
}