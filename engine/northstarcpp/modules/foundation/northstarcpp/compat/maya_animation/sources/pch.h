////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"maya_animation"
#include <northstarcpp/compat/extensions.h>

#define NORTHSTARCPP_DEFAULT_BUILDING
#include <northstarcpp/compat/maya_animation/world.h>

#include "maya_animation_memory.h"

#endif // #ifndef PCH_H_INCLUDED