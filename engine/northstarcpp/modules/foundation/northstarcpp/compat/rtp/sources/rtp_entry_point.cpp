////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "rtp_world.h"
#include <northstarcpp/compat/rtp/api.h>

using northstarcpp::rtp::rtp_world;

static northstarcpp::uninitialized_reference<rtp_world>	s_world;
northstarcpp::rtp::allocator_type*	northstarcpp::rtp::g_allocator = 0;

northstarcpp::rtp::world* northstarcpp::rtp::create_world	( engine& engine )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, rtp_world )	( engine );
	return						( &*s_world );
}

void northstarcpp::rtp::destroy_world					( northstarcpp::rtp::world*& world )
{
	R_ASSERT					( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::rtp::memory_allocator				( memory::doug_lea_allocator_type&  )
{
	ASSERT						( !g_allocator );
	//g_allocator					= &allocator;
	g_allocator = &memory::g_mt_allocator;
}