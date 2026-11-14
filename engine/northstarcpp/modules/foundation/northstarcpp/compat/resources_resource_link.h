////////////////////////////////////////////////////////////////////////////
//	Created		: 04.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCES_RESOURCE_LINK_H_INCLUDED
#define RESOURCES_RESOURCE_LINK_H_INCLUDED

#include <northstarcpp/compat/intrusive_list.h>

namespace northstarcpp {
namespace resources {

class resource_base;
class child_resource_ptr_base;

class NORTHSTARCPP_CORE_API resource_link 
{
public:
	union
	{
		resource_base *				parent;
		child_resource_ptr_base  *	child_member;
	};
	resource_link *					next_link;
	
	resource_link	() : parent(NULL), next_link(NULL) { }

}; // class resource_link

typedef		intrusive_list< resource_link, resource_link, & resource_link::next_link, 
							threading::multi_threading_interlocked_policy >
			resource_link_list;

template class NORTHSTARCPP_CORE_API	intrusive_list< resource_link, resource_link, & resource_link::next_link, 
								threading::multi_threading_interlocked_policy >;

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef RESOURCES_RESOURCE_LINK_H_INCLUDED