////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_API_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_API_H_INCLUDED

#include <northstarcpp/render/api.h>

#ifndef NORTHSTARCPP_RENDER_BASE_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_RENDER_BASE_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_RENDER_BASE_BUILDING
#			define NORTHSTARCPP_RENDER_BASE_API			NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_RENDER_BASE_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RENDER_BASE_API		NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RENDER_BASE_API		NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_RENDER_BASE_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_RENDER_API

namespace northstarcpp {
namespace render {

typedef memory::base_allocator										logic_allocator_type;
typedef memory::base_allocator										editor_allocator_type;
typedef memory::doug_lea_allocator_type								render_allocator_type;

NORTHSTARCPP_RENDER_BASE_API	void	memory_allocator	(
									render_allocator_type& render_allocator,
									logic_allocator_type& logic_allocator,
									editor_allocator_type& editor_allocator
								);

} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_API_H_INCLUDED