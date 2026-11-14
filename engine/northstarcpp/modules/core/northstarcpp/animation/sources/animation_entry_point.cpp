////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "animation_world.h"
#include <northstarcpp/animation/api.h>
#include <northstarcpp/linkage_helper.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
	DECLARE_LINKAGE_ID(animation_entry_point)
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

using northstarcpp::animation::animation_world;

static northstarcpp::uninitialized_reference< animation_world >	s_world;
northstarcpp::animation::allocator_type*	northstarcpp::animation::g_allocator = 0;

northstarcpp::animation::world* northstarcpp::animation::create_world( engine& engine )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, animation_world ) ( engine );
	return						( &*s_world );
}

void northstarcpp::animation::destroy_world( northstarcpp::animation::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::animation::memory_allocator( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}