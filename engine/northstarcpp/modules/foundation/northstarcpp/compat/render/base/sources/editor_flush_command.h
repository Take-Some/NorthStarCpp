////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EDITOR_FLUSH_COMMAND_H_INCLUDED
#define EDITOR_FLUSH_COMMAND_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>
#include <northstarcpp/compat/render/base/editor_renderer.h>
#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace render {
namespace editor {

class editor_renderer;

class flush_command :
	public engine::command,
	private boost::noncopyable
{
public:
	inline				flush_command	( editor_renderer& renderer ): m_renderer			( renderer ){ }

	virtual				~flush_command	( );
	virtual	void		execute			( );

private:
	editor_renderer&	m_renderer;
}; // struct command_delegate

} // namespace editor
} // namespace render
} // namespace northstarcpp

#endif // #ifndef EDITOR_FLUSH_COMMAND_H_INCLUDED