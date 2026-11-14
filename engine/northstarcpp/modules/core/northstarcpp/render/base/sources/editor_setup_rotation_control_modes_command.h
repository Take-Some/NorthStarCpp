////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EDITOR_SETUP_ROTATION_CONTROL_MODES_COMMAND_H_INCLUDED
#define EDITOR_SETUP_ROTATION_CONTROL_MODES_COMMAND_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/editor_renderer.h>
#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace render {
namespace editor {

class editor_renderer;

class setup_rotation_control_modes_command :
	public engine::command,
	private boost::noncopyable
{
public:
	inline	setup_rotation_control_modes_command ( editor_renderer& renderer, bool color_write): 
		m_renderer				( renderer ),
		m_color_write			( color_write )
	{
	}

	virtual				~setup_rotation_control_modes_command ( );
	virtual	void		execute				( );

private:
	editor_renderer&	m_renderer;
	bool	const 			m_color_write;

}; // struct setup_grid_render_mode_command

} // namespace editor
} // namespace render
} // namespace northstarcpp

#endif // #ifndef EDITOR_SETUP_ROTATION_CONTROL_MODES_COMMAND_H_INCLUDED