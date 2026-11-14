////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_NETWORK_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_USE_STDIOSTREAM
#define NORTHSTARCPP_NO_SIGNALS_H

#include "stlport_config.h"

#include <northstarcpp/os_preinclude.h>
#undef NONET

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"network"
#include <northstarcpp/extensions.h>

#include "network_memory.h"

#if !NORTHSTARCPP_PLATFORM_PS3
#	define _WIN32_WINNT 0x0501
#	include "boost_api.h"
#endif // #if !NORTHSTARCPP_PLATFORM_PS3

#endif // #ifndef PCH_H_INCLUDED