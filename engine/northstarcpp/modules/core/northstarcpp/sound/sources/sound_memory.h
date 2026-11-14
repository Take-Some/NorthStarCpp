////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

//#include <northstarcpp/buffer_vector.h>
//#include <northstarcpp/fixed_vector.h>
//#include <northstarcpp/associative_vector.h>
//#include <northstarcpp/hash_multiset.h>

//#include <map>
//#include <vector>
//#include <set>

#include <northstarcpp/sound/api.h>

namespace northstarcpp {
namespace sound {

extern allocator_type*							g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::sound::g_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

} // namespace sound
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::sound::g_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::sound::g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::sound::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::sound::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::sound::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::sound::g_allocator, type, count )

#endif // #ifndef MEMORY_H_INCLUDED