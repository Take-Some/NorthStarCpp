////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_remove_cell.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_terrain_remove_cell;


command_terrain_remove_cell::command_terrain_remove_cell( base_world& world, northstarcpp::render::visual_ptr v, bool beditor )
:m_world		(world),
m_visual		(v),
m_beditor		(beditor)
{
}

command_terrain_remove_cell::~command_terrain_remove_cell	()
{
}

void command_terrain_remove_cell::execute()
{
	m_world.platform().terrain_remove_cell( m_visual, m_beditor );
}