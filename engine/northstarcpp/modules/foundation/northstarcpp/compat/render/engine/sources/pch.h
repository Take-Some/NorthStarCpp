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

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"render_dx10"

#include <northstarcpp/compat/extensions.h>

#define NORTHSTARCPP_RENDER_BUILDING

#include <northstarcpp/compat/render/core/render_include.h>

#include <northstarcpp/compat/render/base/memory.h>

#define R_CHK( expression, ... )											\
	if ( northstarcpp::identity(true) ) {											\
	static bool ignore_always			= false;						\
	if ( !ignore_always && FAILED( northstarcpp::identity(expression) ) ) {		\
	bool do_debug_break = false;									\
	::northstarcpp::debug::on_error	(										\
	& do_debug_break,											\
	northstarcpp::process_error_true,									\
	&ignore_always,												\
	northstarcpp::assert_untyped,										\
	"assertion_failed",											\
	NORTHSTARCPP_MAKE_STRING( expression ),								\
	__FILE__,													\
	__FUNCTION__,												\
	__LINE__,													\
	__VA_ARGS__);												\
	if ( do_debug_break )											\
	DEBUG_BREAK();												\
	}																	\
	} else (void)0

using northstarcpp::math::float2;
using northstarcpp::math::float3;
using northstarcpp::math::color;
using northstarcpp::math::aabb;
using northstarcpp::math::int2;
using northstarcpp::math::uint2;

using namespace northstarcpp;
using northstarcpp::detail::noncopyable;

using northstarcpp::render::vector;
using northstarcpp::render::map;
//using northstarcpp::render::fixed_vector;
//#include <northstarcpp/compat/render/dx10/sources/common/float4x4b.h>
#include <northstarcpp/compat/render/core/utils.h>
#include <northstarcpp/compat/render/core/quasi_singleton.h>

//	Include this before any renderer-specific includes
#include <northstarcpp/compat/render/engine/namespace_wrapper.h>


#include <bitset>

#include <northstarcpp/compat/render/engine/shared_names.h>

// #include <northstarcpp/compat/render/dx10/sources/common/device.h>
//#include <northstarcpp/compat/render/dx10/sources/common/backend_handlers.h>
//#include <northstarcpp/compat/render/dx10/sources/common/backend.h>


// --Porting to DX10_
// //#include <northstarcpp/compat/render/dx10/sources/common/fvf.h>
// #include <northstarcpp/compat/render/dx10/sources/common/manager_common_inline.h>
// 
// #include <northstarcpp/compat/render/dx10/sources/common/res_common.h>
// #include <northstarcpp/compat/render/dx10/sources/common/res_constant_table.h>
// #include <northstarcpp/compat/render/dx10/sources/common/res_declaration.h>
// #include <northstarcpp/compat/render/dx10/sources/common/res_texture.h>
// #include <northstarcpp/compat/render/dx10/sources/common/res_rt.h>
// #include <northstarcpp/compat/render/dx10/sources/common/res_effect.h>
// 
// #include <northstarcpp/compat/render/dx10/sources/common/effect.h>
// #include <northstarcpp/compat/render/dx10/sources/common/effect_compiler.h>
// 
// #include <northstarcpp/compat/render/dx10/sources/common/resource_manager.h>
// //#include <northstarcpp/compat/render/dx10/sources/common/effect_manager.h>
// #include <northstarcpp/compat/render/dx10/sources/common/material_manager.h>
// //#include <northstarcpp/compat/render/dx10/sources/common/lights_db.h>

#include <northstarcpp/compat/render/core/pix_event_wrapper.h>

//#include "visual.h"
#include <northstarcpp/compat/render/engine/render_queue.h>
//#include "model_manager.h"

extern bool g_default_mode;

#endif // #ifndef PCH_H_INCLUDED