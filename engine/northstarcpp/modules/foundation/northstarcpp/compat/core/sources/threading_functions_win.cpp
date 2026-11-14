////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/os_preinclude.h>
#include <northstarcpp/compat/os_include.h>

namespace northstarcpp {
namespace threading {

void set_current_thread_affinity_impl	(u32 const hardware_thread)
{
	SetThreadAffinityMask			(GetCurrentThread(), 1 << hardware_thread);	
}

u32 actual_core_count ( )
{
	SYSTEM_INFO						system_info;
	GetSystemInfo					( &system_info );
	return							system_info.dwNumberOfProcessors;
}

} // namespace threading
} // namespace northstarcpp