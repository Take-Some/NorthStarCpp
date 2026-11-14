////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
//	Description : default entry point functions
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "physics_world.h"
#include <northstarcpp/physics/api.h>

northstarcpp::physics::allocator_type*	northstarcpp::physics::g_allocator = 0;
static NORTHSTARCPP_DEFAULT_ALIGN char	s_world_fake[ sizeof( physics_world ) ];
static physics_world&			s_world			= ( physics_world& )s_world_fake;
static bool						s_initialized	= false;


//northstarcpp::physics::world* northstarcpp::physics::create_world	( northstarcpp::physics::engine& engine, northstarcpp::collision::world &w)
namespace northstarcpp {
namespace physics{

northstarcpp::physics::world* create_world( northstarcpp::physics::engine& engine, northstarcpp::render::debug::renderer& renderer )
{
	R_ASSERT					( !s_initialized );
	s_initialized				= true;
	new ( &s_world ) physics_world	( engine, renderer );
	return						( &s_world );
}

} // namespace physics
} // namespace northstarcpp

void northstarcpp::physics::destroy_world( northstarcpp::physics::world*& world )
{
	R_ASSERT					( s_initialized );
	R_ASSERT					( &s_world == world );
	s_initialized				= false;
	s_world.~physics_world		( );
	world						= 0;
}

void northstarcpp::physics::memory_allocator( allocator_type& allocator )
{
	ASSERT						( !g_allocator );
	g_allocator					= &allocator;
}