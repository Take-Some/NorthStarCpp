////////////////////////////////////////////////////////////////////////////
//	Created		: 03.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_DRAW_VISUAL_H_INCLUDED
#define COMMAND_DRAW_VISUAL_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>
#include <northstarcpp/compat/render/base/visual.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_draw_visual :
	public engine::command,
	private boost::noncopyable
{
public:
							command_draw_visual	( base_world& world, northstarcpp::render::visual_ptr const& object, math::float4x4 const& transform, bool selected = false );
							~command_draw_visual	(  );
	virtual	void			execute				( );

private:
	northstarcpp::render::visual_ptr		m_object;
	math::float4x4						m_transform;
	base_world&						m_world;
	bool								m_selected;
}; // class command_draw_visual

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_DRAW_VISUAL_H_INCLUDED