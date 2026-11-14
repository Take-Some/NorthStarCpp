////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Lain
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef FS_PACK_MEMORY_H_INCLUDED
#define FS_PACK_MEMORY_H_INCLUDED

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>
#include <northstarcpp/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

namespace rms_generator {

extern	northstarcpp::memory::doug_lea_allocator_type	g_rms_generator_allocator;

#define USER_ALLOCATOR							g_rms_generator_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

} // namespace rms_generator 

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		g_rms_generator_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	g_rms_generator_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	g_rms_generator_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	g_rms_generator_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		g_rms_generator_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	g_rms_generator_allocator, type, count )

#endif // #ifndef FS_PACK_MEMORY_H_INCLUDED