////////////////////////////////////////////////////////////////////////////
//	Created 	: 12.11.2008
//	Author		: Lain
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_MEMORY_H_INCLUDED
#define TEST_MEMORY_H_INCLUDED

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>
#include <northstarcpp/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

extern	northstarcpp::memory::doug_lea_allocator_type	g_test_allocator;

#define USER_ALLOCATOR							g_test_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

#define TEST_NEW( type )								NORTHSTARCPP_NEW_IMPL(		g_test_allocator, type )
#define TEST_DELETE( pointer )							NORTHSTARCPP_DELETE_IMPL(	g_test_allocator, pointer )
#define TEST_MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	g_test_allocator, size, description )
#define TEST_REALLOC( pointer, size, description )		NORTHSTARCPP_REALLOC_IMPL(	g_test_allocator, pointer, size, description )
#define TEST_FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		g_test_allocator, pointer )
#define TEST_ALLOC( type, count )						NORTHSTARCPP_ALLOC_IMPL(	g_test_allocator, type, count )

#endif // #ifndef TEST_MEMORY_H_INCLUDED