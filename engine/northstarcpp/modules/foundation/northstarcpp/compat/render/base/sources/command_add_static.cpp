////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_add_static.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>

using northstarcpp::render::base_world;
using northstarcpp::render::command_add_static;
using northstarcpp::resources::managed_resource_ptr;

command_add_static::command_add_static	(
		base_world& world,
		managed_resource_ptr const& collision,
		managed_resource_ptr const& geometry,
		managed_resource_ptr const& visuals
	) :
	m_collision			( collision ),
	m_geometry			( geometry ),
	m_visuals			( visuals ),
	m_world				( world )
{
}

void command_add_static::execute		( )
{
	m_world.platform().add_static	( m_collision, m_geometry, m_visuals );
}