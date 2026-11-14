////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EDITOR_WORLD_INLINE_H_INCLUDED
#define EDITOR_WORLD_INLINE_H_INCLUDED

#pragma managed(push,off)
#include <northstarcpp/compat/editor/world/api.h>
#include <northstarcpp/compat/editor/world/engine.h>

namespace northstarcpp {
namespace editor {

inline engine& editor_world::engine			( )
{
	return	( m_engine );
}

inline gcroot<window_ide^> editor_world::ide	( ) const
{
	return	( m_window_ide );
}

render::editor::renderer&		editor_world::get_renderer			( )
{
	return engine().get_renderer_world().editor();
}

float4x4 editor_world::get_projection_matrix	( )
{
	return m_projection;
}

void editor_world::set_projection_matrix	( math::float4x4 const& matrix )
{
	m_projection = matrix;
}

float4x4 editor_world::get_inverted_view_matrix		( )
{
	return m_inverted_view;
}

void editor_world::set_view_matrix					( float4x4 const& matrix )
{
	m_inverted_view = math::invert4x3(matrix);
}
#pragma managed(pop)

} // namespace editor
} // namespace northstarcpp




#endif // #ifndef EDITOR_WORLD_INLINE_H_INCLUDED