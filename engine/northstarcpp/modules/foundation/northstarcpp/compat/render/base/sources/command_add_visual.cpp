////////////////////////////////////////////////////////////////////////////
//	Created		: 21.01.2010
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>
#include "command_add_visual.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_add_visual;

command_add_visual::command_add_visual( base_world& world, 
										u32 id, 
										northstarcpp::render::visual_ptr const& v, 
										northstarcpp::math::float4x4 const& transform, 
										bool selected, 
										bool system_object,
										bool beditor_chain)
:m_object	( v ),
m_id		( id ),
m_transform	( transform ),
m_world		( world ),
m_selected	( selected ),
m_system	( system_object ),
m_beditor	( beditor_chain )
{
	ASSERT	( id );
}

void command_add_visual::execute( )
{
	m_world.platform().add_visual	( m_id, m_object, m_transform, m_selected, m_system, m_beditor );
}

