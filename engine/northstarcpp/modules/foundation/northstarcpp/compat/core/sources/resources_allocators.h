////////////////////////////////////////////////////////////////////////////
//	Created		: 16.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCES_ALLOCATORS_H_INCLUDED
#define RESOURCES_ALLOCATORS_H_INCLUDED

#include <northstarcpp/compat/memory_fixed_size_allocator.h>
#include <northstarcpp/compat/resources_resource_link.h>
#include "resources_managed_allocator.h"

namespace northstarcpp {
namespace memory {

extern	resources::managed_resource_allocator		g_resources_managed_allocator;
extern	doug_lea_allocator_type						g_resources_unmanaged_allocator;
extern	doug_lea_allocator_type						g_cook_allocator;
extern	fixed_size_allocator<
			northstarcpp::resources::resource_link,
			threading::multi_threading_mutex_policy
		>											g_resources_links_allocator;

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef RESOURCES_ALLOCATORS_H_INCLUDED