////////////////////////////////////////////////////////////////////////////
//	Created		: 20.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RESOURCES_UNMANAGED_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_RESOURCES_UNMANAGED_ALLOCATOR_H_INCLUDED

namespace northstarcpp {
namespace resources {

NORTHSTARCPP_CORE_API managed_resource *		allocate_managed_resource	(u32 size);
NORTHSTARCPP_CORE_API void						free_managed_resource		(managed_resource *);

#define UNMANAGED_NEW( out_pointer, type )	(((out_pointer) = allocate_unmanaged_memory(sizeof(type), typeid(type).name())) == 0) ? 0 : new (type)
#define UNMANAGED_DELETE( pointer )			NORTHSTARCPP_DELETE_IMPL( unmanaged_allocator(), pointer )
#define UNMANAGED_ALLOC( type, count )		(type*)allocate_unmanaged_memory(sizeof(type)*count, typeid(type).name())
#define UNMANAGED_FREE( pointer )			NORTHSTARCPP_FREE_IMPL( unmanaged_allocator(), pointer )

// use UNMANAGED_* macro instead
NORTHSTARCPP_CORE_API memory::base_allocator *	unmanaged_allocator			();
NORTHSTARCPP_CORE_API pvoid						allocate_unmanaged_memory	(u32 size, pcstr type_name);

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RESOURCES_UNMANAGED_ALLOCATOR_H_INCLUDED