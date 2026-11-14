////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "game.h"

using northstarcpp::non_null;
using northstarcpp::raw;

namespace stalker2 {

static northstarcpp::uninitialized_reference<game>	s_world;


northstarcpp::engine_user::allocator_type*	g_allocator = NULL;

northstarcpp::engine_user::world* game_module::create_world	(
		northstarcpp::engine_user::engine& engine, 
		northstarcpp::input::world& input, 
		northstarcpp::render::world& render, 
		northstarcpp::ui::world& ui,
		northstarcpp::physics::world& physics,
		northstarcpp::rtp::world& rtp,
		northstarcpp::animation::world& animation,
		northstarcpp::sound::world& sound
	)
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, game )	( engine, input, render, ui, physics, rtp, animation, sound );

	return						( &*s_world );
}

void game_module::destroy_world						( northstarcpp::engine_user::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void game_module::set_memory_allocator				( northstarcpp::engine_user::allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}

#ifndef NORTHSTARCPP_STATIC_LIBRARIES

static void cleanup						( )
{
	if ( !s_world.initialized() )
		return;

	LOG_ERROR					( "game::world leaked" );

	//raw< northstarcpp::engine_user::world >::ptr world = &*s_world;
	//northstarcpp::game::destroy_world	( world );
}

#include <northstarcpp/compat/os_include.h>

BOOL APIENTRY DllMain					( HANDLE hModule, unsigned int ul_reason_for_call, LPVOID lpReserved )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( hModule, lpReserved );
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH: {
			break;
		}
		case DLL_PROCESS_DETACH: {
			cleanup				( );
			break;
		}
	}
	return						( TRUE );
}

#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

} // namespace stalker2