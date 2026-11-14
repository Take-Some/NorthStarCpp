////////////////////////////////////////////////////////////////////////////
//	Created		: 16.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#ifdef DEBUG
#	include <typeinfo.h>
#endif // #ifdef DEBUG

#include <northstarcpp/memory_macros.h>

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>
#include <northstarcpp/hash_multiset.h>

#include <map>
#include <vector>
#include <set>
#include <deque>

#include <northstarcpp/editor/world/api.h>

namespace northstarcpp {
namespace editor {

extern allocator_type*							g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::editor::g_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

} // namespace input
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::editor::g_allocator, type )
#define DELETE( pointer, ... )					NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::editor::g_allocator, pointer, __VA_ARGS__ )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::editor::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::editor::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::editor::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::editor::g_allocator, type, count )

#endif // #ifndef MEMORY_H_INCLUDED