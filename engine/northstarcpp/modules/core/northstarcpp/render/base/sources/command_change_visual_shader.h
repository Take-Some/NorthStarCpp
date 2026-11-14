////////////////////////////////////////////////////////////////////////////
//	Created		: 09.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_CHANGE_SHADER_H_INCLUDED
#define COMMAND_CHANGE_SHADER_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/visual.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_change_visual_shader :
	public engine::command,
	private boost::noncopyable
{
public:
							command_change_visual_shader	( base_world& world, northstarcpp::render::visual_ptr const& object, fixed_string<256> shader, fixed_string<256> texture );
							~command_change_visual_shader	(  );
	virtual	void			execute				( );

private:
	
	base_world&						m_world;
	northstarcpp::render::visual_ptr		m_object;
	fixed_string<256>					m_shader;
	fixed_string<256>					m_texture;
}; // class command_change_visual_shader

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_CHANGE_SHADER_H_INCLUDED