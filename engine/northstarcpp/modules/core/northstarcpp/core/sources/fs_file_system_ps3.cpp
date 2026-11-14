////////////////////////////////////////////////////////////////////////////
//	Created		: 07.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "fs_file_system.h"
#include "fs_helper.h"
#include "fs_file_system_nodes_impl.h"
#include <northstarcpp/fs_utils.h>
#include <sys/paths.h>

namespace northstarcpp {
namespace fs   {

bool initialize_impl			( )
{
	pcstr const replication_dir		= SYS_DEV_HDD0 "/replication";
 	make_dir						( replication_dir );

	NORTHSTARCPP_CONSTRUCT_REFERENCE		( g_fat, file_system );
	g_fat->set_replication_dir		( replication_dir );

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