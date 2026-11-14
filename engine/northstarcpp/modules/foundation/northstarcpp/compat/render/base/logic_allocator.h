////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_LOGIC_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_LOGIC_ALLOCATOR_H_INCLUDED

#include <northstarcpp/compat/render/base/api.h>

namespace northstarcpp {
namespace render {
namespace logic {

NORTHSTARCPP_RENDER_BASE_API extern logic_allocator_type*	g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::render::logic::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace logic
} // namespace render
} // namespace northstarcpp

#define L_NEW( type )							NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::render::logic::g_allocator, type )
#define L_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::render::logic::g_allocator, pointer )
#define L_MALLOC( size, description )			NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::render::logic::g_allocator, size, description )
#define L_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::render::logic::g_allocator, pointer, size, description )
#define L_FREE( pointer )						NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::render::logic::g_allocator, pointer )
#define L_ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::render::logic::g_allocator, type, count )

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_LOGIC_ALLOCATOR_H_INCLUDED