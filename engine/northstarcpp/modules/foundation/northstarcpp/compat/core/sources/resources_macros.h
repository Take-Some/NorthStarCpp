////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCES_MACROS_H_INCLUDED
#define RESOURCES_MACROS_H_INCLUDED

#include <northstarcpp/compat/memory_doug_lea_allocator.h>

namespace northstarcpp {
namespace memory {

extern  doug_lea_allocator_type						g_resources_helper_allocator;

} // namespace memory
} // namespace northstarcpp

#define RES_NEW( type )			NORTHSTARCPP_NEW_IMPL(		::northstarcpp::memory::g_resources_helper_allocator, type )
#define RES_DELETE( pointer )	NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::memory::g_resources_helper_allocator, pointer )
#define RES_ALLOC( type, count )NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::memory::g_resources_helper_allocator, type, count )
#define RES_FREE( pointer )		NORTHSTARCPP_FREE_IMPL(		::northstarcpp::memory::g_resources_helper_allocator, pointer )

#endif // #ifndef FS_MACROS_H_INCLUDED