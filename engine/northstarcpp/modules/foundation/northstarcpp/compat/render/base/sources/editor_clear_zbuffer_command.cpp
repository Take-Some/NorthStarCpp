////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "editor_clear_zbuffer_command.h"
#include "editor_renderer.h"

namespace northstarcpp {
namespace render {
namespace editor {

clear_zbuffer_command::~clear_zbuffer_command	( )
{
}

void clear_zbuffer_command::execute		( )
{
	m_renderer.clear_zbuffer_impl( m_zvalue );
}

} // namespace editor
} // namespace render
} // namespace northstarcpp
