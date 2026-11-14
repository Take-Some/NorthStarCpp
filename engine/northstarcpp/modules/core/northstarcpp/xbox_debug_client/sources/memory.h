////////////////////////////////////////////////////////////////////////////
//	Created		: 15.01.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
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

extern	northstarcpp::memory::doug_lea_allocator_type	g_allocator;

#define USER_ALLOCATOR							g_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		g_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	g_allocator, type, count )

#endif // #ifndef MEMORY_H_INCLUDED