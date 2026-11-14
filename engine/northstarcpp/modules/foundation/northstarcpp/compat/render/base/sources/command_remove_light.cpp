////////////////////////////////////////////////////////////////////////////
//	Created		: 22.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>
#include "command_remove_light.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_remove_light;

command_remove_light::command_remove_light( base_world& world, u32 id, bool beditor ):
m_world		( world ),
m_id		( id ),
m_beditor	( beditor )
{}
command_remove_light::~command_remove_light	(  )
{

}

void command_remove_light::execute( )
{
	m_world.platform().remove_light( m_id, m_beditor );
}

