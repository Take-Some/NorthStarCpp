////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
//	Description : physics API
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_PHYSICS_API_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_API_H_INCLUDED

#ifndef NORTHSTARCPP_PHYSICS_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_PHYSICS_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_PHYSICS_BUILDING
#			define NORTHSTARCPP_PHYSICS_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_PHYSICS_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_PHYSICS_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_PHYSICS_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_PHYSICS_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_PHYSICS_API

namespace northstarcpp {

namespace render {
	namespace debug {
		struct renderer;
	} // namespace debug
	struct world;
} // namespace render

namespace physics {

struct engine;
struct world;

typedef memory::doug_lea_allocator_type		allocator_type;

NORTHSTARCPP_PHYSICS_API	world*	create_world	( northstarcpp::physics::engine& engine, northstarcpp::render::debug::renderer& renderer );
NORTHSTARCPP_PHYSICS_API	void	destroy_world	( world*& world );
NORTHSTARCPP_PHYSICS_API	void	memory_allocator( allocator_type& allocator );

} // namespace physics
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_PHYSICS_API_H_INCLUDED