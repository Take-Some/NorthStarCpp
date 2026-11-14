////////////////////////////////////////////////////////////////////////////
//	Created		: 07.05.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>
#include <northstarcpp/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

#include <northstarcpp/memory_crt_allocator.h>

namespace northstarcpp {
namespace maya {

typedef memory::doug_lea_allocator_type				allocator_type;
extern allocator_type								g_allocator;

#define USER_ALLOCATOR								::northstarcpp::maya::g_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

typedef memory::crt_allocator						crt_allocator_type;
extern crt_allocator_type							g_crt_allocator;

} // namespace maya
} // namespace northstarcpp

#define NEW( type )									NORTHSTARCPP_NEW_IMPL(		::northstarcpp::maya::g_allocator, type )
#define DELETE( pointer )							NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::maya::g_allocator, pointer )
#define MALLOC( size, description )					NORTHSTARCPP_MALLOC_IMPL(	::northstarcpp::maya::g_allocator, size, description )
#define REALLOC( pointer, size, description )		NORTHSTARCPP_REALLOC_IMPL(	::northstarcpp::maya::g_allocator, pointer, size, description )
#define FREE( pointer )								NORTHSTARCPP_FREE_IMPL(		::northstarcpp::maya::g_allocator, pointer )
#define ALLOC( type, count )						NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::maya::g_allocator, type, count )

#define CRT_NEW( type )								NORTHSTARCPP_NEW_IMPL(		::northstarcpp::maya::g_crt_allocator, type )
#define CRT_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::maya::g_crt_allocator, pointer )
#define CRT_MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	::northstarcpp::maya::g_crt_allocator, size, description )
#define CRT_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	::northstarcpp::maya::g_crt_allocator, pointer, size, description )
#define CRT_FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		::northstarcpp::maya::g_crt_allocator, pointer )
#define CRT_ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::maya::g_crt_allocator, type, count )

#endif // #ifndef MEMORY_H_INCLUDED