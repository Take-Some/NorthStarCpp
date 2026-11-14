////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_cell_add_texture.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include <northstarcpp/memory_macros.h>

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_terrain_cell_add_texture;
using northstarcpp::render::texture_string;


command_terrain_cell_add_texture::command_terrain_cell_add_texture( base_world& world, northstarcpp::render::visual_ptr v, texture_string const & texture, u32 tex_user_id)
:m_world		(world),
m_texture		(texture),
m_visual		(v),
m_tex_user_id	(tex_user_id)
{
}

command_terrain_cell_add_texture::~command_terrain_cell_add_texture	()
{
}

void command_terrain_cell_add_texture::execute()
{
	m_world.platform().terrain_add_cell_texture( m_visual, m_texture.c_str(), m_tex_user_id);
}

