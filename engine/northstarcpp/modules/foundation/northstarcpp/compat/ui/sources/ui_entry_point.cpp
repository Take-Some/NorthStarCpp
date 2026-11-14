#include "pch.h"
#include "ui_world.h"
#include <northstarcpp/compat/ui/api.h>

static northstarcpp::uninitialized_reference<ui_world>	s_world;

northstarcpp::ui::allocator_type* northstarcpp::ui::g_allocator = 0;

northstarcpp::ui::world* northstarcpp::ui::create_world(engine& engine, input::world& input, render::world& render)
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE	( s_world, ui_world)	(engine, input, render);
	return						(&*s_world);
}

void northstarcpp::ui::destroy_world	(northstarcpp::ui::world*& world)
{
	R_ASSERT					(&*s_world == world);
	NORTHSTARCPP_DESTROY_REFERENCE		( s_world );
	world						= 0;
}

void northstarcpp::ui::memory_allocator	(allocator_type& allocator)
{
	ASSERT						(!g_allocator);
	g_allocator					= &allocator;
}