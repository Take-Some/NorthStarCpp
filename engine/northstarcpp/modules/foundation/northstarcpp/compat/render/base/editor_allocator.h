////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_EDITOR_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_EDITOR_ALLOCATOR_H_INCLUDED

#include <northstarcpp/compat/render/base/api.h>

namespace northstarcpp {
namespace render {
namespace editor {

NORTHSTARCPP_RENDER_BASE_API extern editor_allocator_type*				g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::render::editor::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace editor
} // namespace render
} // namespace northstarcpp

#define E_NEW( type )							NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::render::editor::g_allocator, type )
#define E_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::render::editor::g_allocator, pointer )
#define E_MALLOC( size, description )			NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::render::editor::g_allocator, size, description )
#define E_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::render::editor::g_allocator, pointer, size, description )
#define E_FREE( pointer )						NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::render::editor::g_allocator, pointer )
#define E_ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::render::editor::g_allocator, type, count )

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_EDITOR_ALLOCATOR_H_INCLUDED