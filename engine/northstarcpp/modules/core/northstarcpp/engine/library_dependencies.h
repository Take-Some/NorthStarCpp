////////////////////////////////////////////////////////////////////////////
//	Created		: 21.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED
#define NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED

#include <northstarcpp/core/library_linkage.h>

#include <northstarcpp/collision/library_linkage.h>
#include <northstarcpp/physics/library_linkage.h>

#include <northstarcpp/animation/library_linkage.h>
#include <northstarcpp/rtp/library_linkage.h>

#include <northstarcpp/network/library_linkage.h>

#include <northstarcpp/sound/library_linkage.h>

#include <northstarcpp/input/library_linkage.h>

#include <northstarcpp/render/base/library_linkage.h>
#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	include <northstarcpp/render/library_linkage.h>
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#include <northstarcpp/ui/library_linkage.h>

#include <northstarcpp/editor/world/library_linkage.h>

#endif // #ifndef NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED