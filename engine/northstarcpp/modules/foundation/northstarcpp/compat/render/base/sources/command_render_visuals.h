////////////////////////////////////////////////////////////////////////////
//	Created		: 25.01.2010
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_RENDER_VISUALS_H_INCLUDED
#define COMMAND_RENDER_VISUALS_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_render_visuals :
	public engine::command,
	private boost::noncopyable
{
public:
							command_render_visuals	( base_world& world );
							~command_render_visuals	(  );
	virtual	void			execute				( );

private:
	base_world&						m_world;
}; // class command_render_visuals

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_RENDER_VISUALS_H_INCLUDED