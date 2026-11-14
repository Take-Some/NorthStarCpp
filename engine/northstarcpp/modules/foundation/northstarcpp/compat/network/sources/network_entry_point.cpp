////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "network_world.h"
#include <northstarcpp/compat/network/api.h>

using northstarcpp::network::network_world;

static northstarcpp::uninitialized_reference<network_world>	s_world;
northstarcpp::network::allocator_type*	northstarcpp::network::g_allocator = 0;

northstarcpp::network::world* northstarcpp::network::create_world	( engine& engine )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, network_world )	( engine );
	return						( &*s_world );
}

void northstarcpp::network::destroy_world					( northstarcpp::network::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::network::memory_allocator				( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}

void northstarcpp::network::dispatch_callbacks				( )
{
}

// here is _STLP_DECLSPEC
_STLP_DECLSPEC