////////////////////////////////////////////////////////////////////////////
//	Created		: 03.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/base/world.h>
#include <northstarcpp/compat/render/base/platform.h>
#include "command_change_visual_shader.h"

using northstarcpp::render::base_world;
using northstarcpp::render::command_change_visual_shader;

command_change_visual_shader::command_change_visual_shader( base_world& world, northstarcpp::render::visual_ptr const& object, northstarcpp::fixed_string<256> shader, northstarcpp::fixed_string<256> texture )
:m_world		(world),
m_object		(object),
m_shader		(shader),
m_texture		(texture)
{

}

command_change_visual_shader::~command_change_visual_shader	(  )
{

}

void command_change_visual_shader::execute( )
{
	m_world.platform().change_visual_shader(m_object,m_shader.c_str(),m_texture.c_str());
}

