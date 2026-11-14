////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_view_matrix.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>

using northstarcpp::render::command_view_matrix;

command_view_matrix::command_view_matrix( northstarcpp::render::platform& platform, northstarcpp::float4x4 const& view ) :
	m_view						( view ),
	m_platform					( platform )
{
}

void command_view_matrix::execute		( )
{
	m_platform.set_view_matrix	( m_view );
}