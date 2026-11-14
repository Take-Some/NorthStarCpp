////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_COLLISION_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"collision"
#include <northstarcpp/extensions.h>

#endif // #ifndef PCH_H_INCLUDED