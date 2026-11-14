////////////////////////////////////////////////////////////////////////////
//	Created		: 21.01.2010
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include "command_remove_visual.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_remove_visual;

command_remove_visual::command_remove_visual( base_world& world, u32 id ):
m_world(world),
m_id(id)
{}
command_remove_visual::~command_remove_visual	(  )
{

}

void command_remove_visual::execute( )
{
	m_world.platform().remove_visual( m_id );
}

