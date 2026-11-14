////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/api.h>

typedef northstarcpp::render::render_allocator_type	render_allocator_type;
typedef northstarcpp::render::logic_allocator_type	logic_allocator_type;
typedef northstarcpp::render::editor_allocator_type	editor_allocator_type;

using northstarcpp::render::base_world;

static northstarcpp::uninitialized_reference<base_world>	s_world;

render_allocator_type*	northstarcpp::render::g_allocator = 0;
logic_allocator_type*	northstarcpp::render::logic::g_allocator = 0;
editor_allocator_type*	northstarcpp::render::editor::g_allocator = 0;

void northstarcpp::render::memory_allocator		(
		render_allocator_type&	render_allocator,
		logic_allocator_type&	logic_allocator,
		editor_allocator_type&	editor_allocator
	)
{
	R_ASSERT			( !g_allocator );
	g_allocator			= &render_allocator;

	R_ASSERT			( !logic::g_allocator );
	logic::g_allocator	= &logic_allocator;

	R_ASSERT			( !editor::g_allocator );
	editor::g_allocator	= &editor_allocator;
}

northstarcpp::render::world* northstarcpp::render::create_world	( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE( s_world, base_world)	( wrapper, window_handle );
	return					( &*s_world );
}

void northstarcpp::render::destroy_world					( northstarcpp::render::world*& world )
{
	R_ASSERT				( &*s_world == world );
	NORTHSTARCPP_DESTROY_REFERENCE	( s_world );
	world					= 0;
}