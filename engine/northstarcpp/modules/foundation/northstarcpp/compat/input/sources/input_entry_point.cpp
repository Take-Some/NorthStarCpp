////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "input_world.h"
#include <northstarcpp/compat/input/api.h>

using northstarcpp::input::input_world;

static northstarcpp::uninitialized_reference<input_world>	s_world;
northstarcpp::input::allocator_type*	northstarcpp::input::g_allocator = 0;

northstarcpp::input::world* northstarcpp::input::create_world	( engine& engine, HWND const window_handle )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, input_world )	( engine, window_handle );
	return						( &*s_world );
}

void northstarcpp::input::destroy_world					( northstarcpp::input::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::input::memory_allocator				( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}