////////////////////////////////////////////////////////////////////////////
//	Created		: 19.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEXTURE_COMPRESSOR_TEST_MEMORY_H_INCLUDED
#define TEXTURE_COMPRESSOR_TEST_MEMORY_H_INCLUDED

#include <northstarcpp/compat/buffer_vector.h>
#include <northstarcpp/compat/fixed_vector.h>
#include <northstarcpp/compat/associative_vector.h>
#include <northstarcpp/compat/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

namespace northstarcpp {
namespace texture_compressor_test {

extern	northstarcpp::memory::doug_lea_allocator_type	g_allocator;

#define USER_ALLOCATOR							g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace texture_compressor_test
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		northstarcpp::texture_compressor_test::g_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	northstarcpp::texture_compressor_test::g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	northstarcpp::texture_compressor_test::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	northstarcpp::texture_compressor_test::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		northstarcpp::texture_compressor_test::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	northstarcpp::texture_compressor_test::g_allocator, type, count )

#endif // #ifndef TEXTURE_COMPRESSOR_TEST_MEMORY_H_INCLUDED