////////////////////////////////////////////////////////////////////////////
//	Created		: 09.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_projection_matrix.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>

using northstarcpp::render::command_projection_matrix;

command_projection_matrix::command_projection_matrix( northstarcpp::render::platform& platform, northstarcpp::float4x4 const& projection ) :
m_projection						( projection ),
m_platform					( platform )
{
}

void command_projection_matrix::execute		( )
{
	m_platform.set_projection_matrix	( m_projection );
}