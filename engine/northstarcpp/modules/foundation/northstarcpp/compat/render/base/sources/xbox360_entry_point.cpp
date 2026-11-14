////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "xbox360_world.h"
#include <northstarcpp/compat/render/api.h>
#include <northstarcpp/compat/render/base/api.h>

using northstarcpp::render::xbox360::world;

static northstarcpp::uninitialized_reference<world>	s_world;

northstarcpp::render::world* northstarcpp::render::xbox360::create_world	( render::engine::wrapper& wrapper )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE( s_world, xbox360::world )	( wrapper );
	return					( &*s_world );
}

void northstarcpp::render::xbox360::destroy_world							( northstarcpp::render::world*& world )
{
	R_ASSERT				( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE	( s_world );
	world					= 0;
}