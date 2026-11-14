////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "editor_flush_command.h"
#include "editor_renderer.h"

namespace northstarcpp {
namespace render {
namespace editor {

flush_command::~flush_command	( )
{
}

void flush_command::execute		( )
{
	m_renderer.flush_impl();
}

} // namespace editor
} // namespace render
} // namespace northstarcpp