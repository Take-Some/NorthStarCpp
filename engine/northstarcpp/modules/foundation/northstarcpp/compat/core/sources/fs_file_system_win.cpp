////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "fs_file_system.h"
#include "fs_helper.h"
#include "fs_file_system_nodes_impl.h"

namespace northstarcpp {
namespace fs   {

NORTHSTARCPP_CORE_API bool	make_dir(pcstr path);

bool initialize_impl			( )
{
	pcstr const replications_dir	= "c:/replication";
 	make_dir						( replications_dir );

	NORTHSTARCPP_CONSTRUCT_REFERENCE		( g_fat, file_system );
	g_fat->set_replication_dir		( replications_dir );
	
	return							( true );
}

void finalize_impl ( )
{
	NORTHSTARCPP_DESTROY_REFERENCE			( g_fat );
}

void flush_replications	( )
{
}

} // namespace fs
} // namespace northstarcpp