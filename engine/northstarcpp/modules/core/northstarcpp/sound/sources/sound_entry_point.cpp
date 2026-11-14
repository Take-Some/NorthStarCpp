////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "sound_world.h"
#include <northstarcpp/sound/api.h>
#include <northstarcpp/linkage_helper.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
	DECLARE_LINKAGE_ID(sound_entry_point)
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

using northstarcpp::sound::sound_world;

static northstarcpp::uninitialized_reference<sound_world>	s_world;

northstarcpp::sound::allocator_type*	northstarcpp::sound::g_allocator = 0;

northstarcpp::sound::world* northstarcpp::sound::create_world	( engine& engine, base_allocator_type* logic_allocator, base_allocator_type* editor_allocator )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, sound_world )	( engine, logic_allocator, editor_allocator );
	return						( &*s_world );
}

void northstarcpp::sound::destroy_world					( northstarcpp::sound::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::sound::memory_allocator				( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}

void northstarcpp::sound::dispatch_logic_callbacks( )
{
	s_world->process_logic_callback_orders();
}

void northstarcpp::sound::dispatch_editor_callbacks( )
{
	s_world->process_editor_callback_orders();
}
