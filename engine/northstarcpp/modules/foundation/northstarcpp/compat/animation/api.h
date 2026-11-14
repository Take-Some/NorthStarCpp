////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ANIMATION_API_H_INCLUDED
#define NORTHSTARCPP_ANIMATION_API_H_INCLUDED

#ifndef NORTHSTARCPP_ANIMATION_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_ANIMATION_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_ANIMATION_BUILDING
#			define NORTHSTARCPP_ANIMATION_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_ANIMATION_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_ANIMATION_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_ANIMATION_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_ANIMATION_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_ANIMATION_API

namespace northstarcpp {
namespace animation {

struct engine;
struct world;

typedef memory::doug_lea_allocator_type		allocator_type;

NORTHSTARCPP_ANIMATION_API	world*	create_world	( engine& engine );
NORTHSTARCPP_ANIMATION_API	void	destroy_world	( world*& world );
NORTHSTARCPP_ANIMATION_API	void	memory_allocator( allocator_type& allocator );

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ANIMATION_API_H_INCLUDED