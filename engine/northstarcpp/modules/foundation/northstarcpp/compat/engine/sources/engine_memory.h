////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <northstarcpp/compat/buffer_vector.h>
#include <northstarcpp/compat/fixed_vector.h>
#include <northstarcpp/compat/associative_vector.h>
#include <northstarcpp/compat/hash_multiset.h>

namespace northstarcpp {
namespace engine {

extern memory::doug_lea_allocator_type			g_allocator;

#define USER_ALLOCATOR							::northstarcpp::engine::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace engine
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		::northstarcpp::engine::g_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::engine::g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	::northstarcpp::engine::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	::northstarcpp::engine::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		::northstarcpp::engine::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::engine::g_allocator, type, count )

#endif // #ifndef MEMORY_H_INCLUDED