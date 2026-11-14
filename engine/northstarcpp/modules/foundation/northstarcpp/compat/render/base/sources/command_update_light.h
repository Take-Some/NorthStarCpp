////////////////////////////////////////////////////////////////////////////
//	Created		: 23.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_UPDATE_LIGHT_H_INCLUDED
#define COMMAND_UPDATE_LIGHT_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>
#include <northstarcpp/compat/render/base/light_props.h>
#include <northstarcpp/compat/render/base/visual.h>

namespace northstarcpp {
namespace render {

class light_props;

class base_world;

class command_update_light :
	public engine::command,
	private boost::noncopyable
{
public:
					command_update_light	( base_world& world, u32 id, light_props const& props, bool beditor );
	virtual			~command_update_light	(  );
	virtual	void	execute					( );

private:
	northstarcpp::render::visual_ptr	m_object;
	u32							m_id;
	base_world&					m_world;
	light_props					m_props;
	bool						m_beditor;
}; // class command_update_light

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_UPDATE_LIGHT_H_INCLUDED