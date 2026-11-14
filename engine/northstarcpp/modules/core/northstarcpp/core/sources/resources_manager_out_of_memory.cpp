////////////////////////////////////////////////////////////////////////////
//	Created		: 14.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resources_manager.h"

namespace northstarcpp {
namespace resources {

void   resources_manager::out_of_memory_functionality::push_out_of_memory_query_impl (query_result * query)
{
	m_out_of_memory_queue.push_back					(query);
	g_resources_manager->wakeup_resources_thread	();
}

void   resources_manager::out_of_memory_functionality::tick ()
{
	while ( query_result * const query = m_out_of_memory_queue.pop_front() )
	{

	}
}


} // namespace resources
} // namespace northstarcpp

