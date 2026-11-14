////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_RENDER_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_RENDER_ALLOCATOR_H_INCLUDED

#include <northstarcpp/compat/render/base/api.h>

namespace northstarcpp {
namespace render {

NORTHSTARCPP_RENDER_BASE_API extern render_allocator_type*	g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::render::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace render
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::render::g_allocator, type )
#define NEW_ARRAY(type, count)					NORTHSTARCPP_NEW_ARRAY_IMPL(*::northstarcpp::render::g_allocator, type, count)
#define DELETE_ARRAY(pointer)					NORTHSTARCPP_DELETE_ARRAY_IMPL(*::northstarcpp::render::g_allocator, pointer )
#define DELETE( pointer, ... )					NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::render::g_allocator, pointer, __VA_ARGS__ )
#define DELETET( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::render::g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::render::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::render::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::render::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::render::g_allocator, type, count )

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_RENDER_ALLOCATOR_H_INCLUDED