////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_ANIMATION_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"animation"
#include <northstarcpp/compat/extensions.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS_32 && !defined(MASTER_GOLD)
#	define	NORTHSTARCPP_USE_MAYA_ANIMATION	1
#else // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 && !defined(MASTER_GOLD)
#	define	NORTHSTARCPP_USE_MAYA_ANIMATION	0
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 && !defined(MASTER_GOLD)

#include "animation_memory.h"

#endif // #ifndef PCH_H_INCLUDED