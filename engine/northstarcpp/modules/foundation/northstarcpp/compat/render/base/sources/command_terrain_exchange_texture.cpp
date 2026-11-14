////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_exchange_texture.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>
#include <northstarcpp/compat/memory_macros.h>

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_terrain_exchange_texture;
using northstarcpp::render::texture_string;


command_terrain_exchange_texture::command_terrain_exchange_texture( base_world& world, texture_string const& old_texture, texture_string const& new_texture)
:m_world		(world),
m_old			(old_texture),
m_new			(new_texture)

{
}

void command_terrain_exchange_texture::execute()
{
	m_world.platform().terrain_exchange_texture( m_old.c_str(), m_new.c_str());
}