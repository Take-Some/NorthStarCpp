////////////////////////////////////////////////////////////////////////////
//	Created		: 30.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "ui_image.h"
#include "ui_quad.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/ui_renderer.h>

ui_image::ui_image()
:m_color(0xffffffff),m_tex_coords(0.0f,0.0f,1.0f,1.0f), m_point_type(1)
{}

ui_image::~ui_image()
{}

void ui_image::init_texture(pcstr texture_name)
{
	if(0==strings::compare_insensitive(texture_name,"ui_scull"))
	{
		m_tex_coords.set(0,0,1,1);
		m_point_type	=	1;
	}else
	if(0==strings::compare_insensitive(texture_name,"ui_rect"))
	{
		m_tex_coords.set(0,0,1,1);
		m_point_type	=	2;
	}else
		UNREACHABLE_CODE();
}

void ui_image::draw(northstarcpp::render::world& w)
{
	int prim_type			= 0;
	
	float2 pos				= get_position();
	float2 size				= get_size();
	client_to_screen		(*this, pos);		//need absolute position

	u32 max_verts			= 1*4; // use TriangleList+ IndexBuffer(0-1-2, 3-2-1)

	northstarcpp::render::ui::command* cmd = w.ui().create_command(max_verts, prim_type, m_point_type);
	{
        for (const auto& corner : northstarcpp::ui::quad_corners(
                 pos.x, pos.y, pos.x + size.x, pos.y + size.y,
                 m_tex_coords.x, m_tex_coords.y, m_tex_coords.z, m_tex_coords.w))
            cmd->push_point(corner.x, corner.y, 0.0f, m_color, corner.u, corner.v);
	}
//	cmd->set_shader();
//	cmd->set_geom();
	w.push_command			(cmd);

	ui_window::draw(w);
}