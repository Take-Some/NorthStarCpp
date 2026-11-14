////////////////////////////////////////////////////////////////////////////
//	Created		: 09.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RESOURCES_MEMORY_H_INCLUDED
#define NORTHSTARCPP_RESOURCES_MEMORY_H_INCLUDED

namespace northstarcpp {
namespace resources {

class memory_type
{
public:
					memory_type		(pcstr name) : m_name(name) {}
private:
	pcstr			m_name;
};

extern memory_type NORTHSTARCPP_CORE_API 	memory_type_non_cacheable_resource;
extern memory_type NORTHSTARCPP_CORE_API	memory_type_managed_resource;
extern memory_type NORTHSTARCPP_CORE_API	memory_type_unmanaged_resource;

class NORTHSTARCPP_CORE_API memory_usage
{
public:
					memory_usage	() : type(0), pool(0), size(0) { ; }
					memory_usage	(memory_type const * type, u32 size, u32 pool = 0) : type(type), size(size), pool(pool) { ; }
public:
	memory_type const *	type;
	u32					pool;
	u32					size;
};

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RESOURCES_MEMORY_H_INCLUDED