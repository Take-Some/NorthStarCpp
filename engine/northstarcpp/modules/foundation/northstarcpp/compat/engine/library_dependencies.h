////////////////////////////////////////////////////////////////////////////
//	Created		: 21.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED
#define NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED

#include <northstarcpp/compat/core/library_linkage.h>

#include <northstarcpp/compat/collision/library_linkage.h>
#include <northstarcpp/compat/physics/library_linkage.h>

#include <northstarcpp/compat/animation/library_linkage.h>
#include <northstarcpp/compat/rtp/library_linkage.h>

#include <northstarcpp/compat/network/library_linkage.h>

#include <northstarcpp/compat/sound/library_linkage.h>

#include <northstarcpp/compat/input/library_linkage.h>

#include <northstarcpp/compat/render/base/library_linkage.h>
#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	include <northstarcpp/compat/render/library_linkage.h>
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#include <northstarcpp/compat/ui/library_linkage.h>

#include <northstarcpp/compat/editor/world/library_linkage.h>

#endif // #ifndef NORTHSTARCPP_ENGINE_LIBRARY_DEPENDENCIES_H_INCLUDED