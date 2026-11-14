////////////////////////////////////////////////////////////////////////////
//	Created		: 22.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_add_light.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>

using northstarcpp::render::base_world;
using northstarcpp::render::command_add_light;

command_add_light::command_add_light( base_world& world, u32 id, northstarcpp::render::light_props const& props, bool beditor)
:m_id		( id ),
m_world		( world ),
m_props		( props ),
m_beditor	( beditor )
{
	ASSERT(id);
}

command_add_light::~command_add_light	(  )
{

}

void command_add_light::execute( )
{
 	m_world.platform().add_light( m_id, m_props, m_beditor );
}

