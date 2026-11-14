////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug_draw_triangles_command.h"
#include "debug_renderer.h"

using northstarcpp::render::debug::draw_triangles_command;
using northstarcpp::render::platform;
using northstarcpp::render::vertex_colored;

draw_triangles_command::~draw_triangles_command	( )
{
}

void draw_triangles_command::execute			( )
{
	m_renderer.draw_triangles_impl	( m_vertices, m_indices );
}