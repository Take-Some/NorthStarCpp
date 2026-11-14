////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"render_dx9"
#include <northstarcpp/compat/extensions.h>

#define NORTHSTARCPP_RENDER_BUILDING
#include <northstarcpp/compat/render/api.h>

#include <northstarcpp/compat/render/dx9/render_include.h>

#include <northstarcpp/compat/render/base/memory.h>


using northstarcpp::math::float2;
using northstarcpp::math::float3;
using northstarcpp::math::color;
using northstarcpp::math::aabb;
using northstarcpp::math::int2;
using northstarcpp::math::uint2;

using namespace northstarcpp;

using northstarcpp::render::vector;
using northstarcpp::render::map;
//using northstarcpp::render::fixed_vector;

#include <northstarcpp/compat/render/common/sources/d3d_defs.h>
#include <northstarcpp/compat/render/dx9/dx9_include.h>

//#include "visual.h"
#include "render_queue.h"
//#include "model_manager.h"

#endif // #ifndef PCH_H_INCLUDED