////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_terrain_add_cell.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include <northstarcpp/memory_macros.h>

namespace northstarcpp{
namespace render{

command_terrain_add_cell::command_terrain_add_cell(	base_world& world, visual_ptr v, bool beditor )
:m_world					(world),
m_visual					(v),
m_beditor					(beditor)
{
}

command_terrain_add_cell::~command_terrain_add_cell	()
{
}

void command_terrain_add_cell::execute()
{
	m_world.platform().terrain_add_cell( m_visual, m_beditor );
}

} //namespace render
} //namespace northstarcpp
