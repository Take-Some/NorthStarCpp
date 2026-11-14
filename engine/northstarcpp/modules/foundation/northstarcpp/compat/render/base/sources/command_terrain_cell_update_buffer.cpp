////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_cell_update_buffer.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_terrain_cell_update_buffer;
using northstarcpp::render::buffer_fragment_NEW;

command_terrain_cell_update_buffer::command_terrain_cell_update_buffer(	base_world& world, 
																		northstarcpp::render::visual_ptr v, 
																		northstarcpp::vectora<buffer_fragment_NEW> & fragments,
																		northstarcpp::math::float4x4 const& transform,
																		northstarcpp::render::command_finished_callback const& finish_callback,
																		u32 finish_callback_argument)
:m_world					(world),
m_visual					(v),
m_fragments					(fragments.get_allocator()),
m_transform					(transform),
m_finish_callback			(finish_callback),
m_finish_callback_argument	(finish_callback_argument)
{
	m_fragments.swap(fragments);
}

command_terrain_cell_update_buffer::~command_terrain_cell_update_buffer	()
{
	if( m_finish_callback)
		m_finish_callback( m_finish_callback_argument);
}

void command_terrain_cell_update_buffer::execute()
{
	m_world.platform().terrain_update_cell_buffer( m_visual, m_fragments, m_transform );
}

