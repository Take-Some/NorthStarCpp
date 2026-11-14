////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EDITOR_CLEAR_ZBUFFER_COMMAND_H_INCLUDED
#define EDITOR_CLEAR_ZBUFFER_COMMAND_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/editor_renderer.h>
#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace render {
namespace editor {

class editor_renderer;

class clear_zbuffer_command :
	public engine::command,
	private boost::noncopyable
{
public:
	inline				clear_zbuffer_command	( editor_renderer& renderer, float zvalue ): 
						m_renderer				( renderer ),
						m_zvalue				( zvalue )
	{
	}

	virtual				~clear_zbuffer_command	( );
	virtual	void		execute					( );

private:
	editor_renderer&	m_renderer;
	float				m_zvalue;
}; // struct command_delegate

} // namespace editor
} // namespace render
} // namespace northstarcpp


#endif // #ifndef EDITOR_CLEAR_ZBUFFER_COMMAND_H_INCLUDED