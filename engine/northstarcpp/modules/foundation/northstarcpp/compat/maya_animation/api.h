////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DEFAULT_API_H_INCLUDED
#define NORTHSTARCPP_DEFAULT_API_H_INCLUDED

#include <northstarcpp/compat/maya_animation/world.h>
#include <northstarcpp/compat/maya_animation/engine.h>

#ifndef NORTHSTARCPP_DEFAULT_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_MAYA_ANIMATION_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_MAYA_ANIMATION_BUILDING
#			define NORTHSTARCPP_MAYA_ANIMATION_API			NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_MAYA_ANIMATION_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_MAYA_ANIMATION_API		NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_MAYA_ANIMATION_API		NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_MAYA_ANIMATION_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_DEFAULT_API

namespace northstarcpp {
namespace maya_animation {

typedef memory::doug_lea_allocator_type		allocator_type;

NORTHSTARCPP_MAYA_ANIMATION_API	world*	create_world	( engine& engine );
NORTHSTARCPP_MAYA_ANIMATION_API	void	destroy_world	( world*& world );
NORTHSTARCPP_MAYA_ANIMATION_API	void	memory_allocator( allocator_type& allocator );

} // namespace maya_animation
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_DEFAULT_API_H_INCLUDED