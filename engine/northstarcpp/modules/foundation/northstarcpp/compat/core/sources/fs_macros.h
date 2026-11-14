////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef FS_MACROS_H_INCLUDED
#define FS_MACROS_H_INCLUDED

#include "memory_stack_arena_allocator.h"
#include <northstarcpp/compat/memory_synchronized_allocator.h>

namespace northstarcpp {
namespace memory {

typedef memory::memory_synchronized_allocator<memory::doug_lea_allocator_type>	fs_allocator;
//typedef memory::doug_lea_allocator_type	fs_allocator;
extern	fs_allocator				g_fs_allocator;

} // namespace memory
} // namespace northstarcpp

#define FS_NEW( type )			NORTHSTARCPP_NEW_IMPL(		::northstarcpp::memory::g_fs_allocator, type )
#define FS_DELETE( pointer )	NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::memory::g_fs_allocator, pointer )
#define FS_ALLOC( type, count )	NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::memory::g_fs_allocator, type, count )
#define FS_FREE( pointer )		NORTHSTARCPP_FREE_IMPL(		::northstarcpp::memory::g_fs_allocator, pointer )

#endif // #ifndef FS_MACROS_H_INCLUDED