////////////////////////////////////////////////////////////////////////////
//	Created		: 17.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define	NORTHSTARCPP_LOG_MODULE_INITIATOR	"engine_pc"

#include <northstarcpp/macro_extensions.h>

#include <northstarcpp/engine/library_dependencies.h>

#define NORTHSTARCPP_ENGINE_LIBRARY_BUILDING
#include <northstarcpp/engine/library_linkage.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES

#include <northstarcpp/type_extensions.h>
#include <boost/noncopyable.hpp>
#include <northstarcpp/detail_noncopyable.h>
#include <northstarcpp/debug_macros.h>
#include <northstarcpp/memory_base_allocator.h>
#include <stdio.h>
#include <northstarcpp/os_extensions.h>
#include <northstarcpp/os_include.h>
#include <northstarcpp/debug_macros.h>
#include <northstarcpp/engine/api.h>

#include "linkage_resolver.h"


BOOL APIENTRY DllMain							( HANDLE hModule, unsigned int ul_reason_for_call, LPVOID lpReserved )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( hModule, lpReserved );

	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH: {
			break;
		}
		case DLL_PROCESS_DETACH: {
			break;
		}
	}
	return						( TRUE );
}

#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES