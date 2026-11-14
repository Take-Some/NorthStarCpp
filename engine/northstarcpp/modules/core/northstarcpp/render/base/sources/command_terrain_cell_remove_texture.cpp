////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_cell_remove_texture.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_terrain_cell_remove_texture;
using northstarcpp::render::texture_string;

command_terrain_cell_remove_texture::command_terrain_cell_remove_texture( base_world& world, northstarcpp::render::visual_ptr v, u32 tex_user_id)
:m_world		(world),
m_visual		(v),
m_tex_user_id	(tex_user_id)
{
}

command_terrain_cell_remove_texture::~command_terrain_cell_remove_texture	()
{
}

void command_terrain_cell_remove_texture::execute()
{
	m_world.platform().terrain_remove_cell_texture( m_visual, m_tex_user_id);
}

