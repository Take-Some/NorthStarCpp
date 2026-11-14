////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "maya_animation_world.h"
#include <northstarcpp/compat/maya_animation/api.h>
#include <northstarcpp/compat/linkage_helper.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
	DECLARE_LINKAGE_ID(maya_animation_entry_point)
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

using northstarcpp::maya_animation::maya_animation_world;

static northstarcpp::uninitialized_reference< maya_animation_world >	s_world;

northstarcpp::maya_animation::allocator_type*	northstarcpp::maya_animation::g_allocator = 0;

northstarcpp::maya_animation::world* northstarcpp::maya_animation::create_world( engine& engine )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, maya_animation_world )	( engine );
	return						( &*s_world );
}

void northstarcpp::maya_animation::destroy_world( northstarcpp::maya_animation::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::maya_animation::memory_allocator( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}