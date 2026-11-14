////////////////////////////////////////////////////////////////////////////
//	Created		: 21.01.2010
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/base/world.h>
#include "command_render_visuals.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_render_visuals;

command_render_visuals::command_render_visuals( base_world& world )
:m_world(world)
{}

command_render_visuals::~command_render_visuals	(  )
{

}

void command_render_visuals::execute( )
{
//	m_world.render_visuals			( );
}

