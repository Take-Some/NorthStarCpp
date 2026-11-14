////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "fs_file_system.h"
#include "fs_helper.h"
#include <northstarcpp/fs_utils.h>
#include "northstarcpp/os_include.h"
#include <xbox.h>

namespace northstarcpp {
namespace fs   {

NORTHSTARCPP_CORE_API bool	make_dir(pcstr path);

bool initialize_impl			( )
{
	long const result				= XMountUtilityDrive( false, 64*1024, 64*1024 );
	NORTHSTARCPP_UNREFERENCED_PARAMETER		( result );
	R_ASSERT						( result == ERROR_SUCCESS );

	pcstr const replications_dir	= "cache:/replication";
 	make_dir						( replications_dir );

	NORTHSTARCPP_CONSTRUCT_REFERENCE		( g_fat, file_system );
	g_fat->set_replication_dir		( replications_dir );
	
	return							true;
}

void finalize_impl ( )
{
	NORTHSTARCPP_DESTROY_REFERENCE			( g_fat );
}

void flush_replications	( )
{
	XFlushUtilityDrive				( );
}

} // namespace fs
} // namespace northstarcpp