////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_RENDER_BASE_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"render_base"
#include <northstarcpp/extensions.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS
#	include <northstarcpp/render/dx9/render_include.h>
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS

//Macros delete is declared in windows headers, so this is small hack to avoid problems
#undef DELETE
#include <northstarcpp/render/base/memory.h>

using northstarcpp::math::float2;
using northstarcpp::math::float3;
using northstarcpp::math::color;

#endif // #ifndef PCH_H_INCLUDED