////////////////////////////////////////////////////////////////////////////
//	Created		: 27.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RESOURCES_CALLBACKS_H_INCLUDED
#define NORTHSTARCPP_RESOURCES_CALLBACKS_H_INCLUDED

#include <northstarcpp/resources_memory.h>

namespace northstarcpp {
namespace resources {

class					resource_base;
typedef					boost::function< void ( resource_base * resource ) >	query_finished_callback;
NORTHSTARCPP_CORE_API	void	set_query_finished_callback	( query_finished_callback callback );

enum out_of_memory_treatment_enum	{	out_of_memory_treatment_failed,
										out_of_memory_treatment_reallocate_synchronously,
										out_of_memory_treatment_reallocate_asynchronously,	};

typedef					boost::function< out_of_memory_treatment_enum ( memory_usage const & memory_needed ) >	
						out_of_memory_callback;

NORTHSTARCPP_CORE_API	void	set_out_of_memory_callback	( out_of_memory_callback callback );

// set_on_resource_leaked_callback is implemented in fs

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RESOURCES_CALLBACKS_H_INCLUDED