////////////////////////////////////////////////////////////////////////////
//	Created		: 03.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include "command_update_visual.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_update_visual;

command_update_visual::command_update_visual( base_world& world, u32 id, northstarcpp::math::float4x4 const& transform, bool selected )
:m_world		(world),
m_id		(id),
m_transform	(transform),
m_selected	(selected)
{

}

command_update_visual::~command_update_visual	(  )
{

}

void command_update_visual::execute( )
{
	m_world.platform().update_visual( m_id, m_transform, m_selected);
}

