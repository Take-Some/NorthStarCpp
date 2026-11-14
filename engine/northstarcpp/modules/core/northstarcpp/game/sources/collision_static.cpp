////////////////////////////////////////////////////////////////////////////
//	Created		: 23.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "collision_static.h"
#include <northstarcpp/collision/api.h>
#include <northstarcpp/physics/world.h>
#include <northstarcpp/collision/object.h>
#include <northstarcpp/collision/geometry_instance.h>

namespace stalker2{

enum {
	object_type_geometry	= u32(1) << 0,
	object_type_object		= u32(1) << 1,
}; // enum

collision_static::collision_static():
	m_collision			( 0 ),
	m_collision_object	( 0 ),
	m_statics_shell		( 0 )
{
}

void collision_static::create( northstarcpp::resources::managed_resource_ptr const mesh_resource, physics::world &w  )
{
	ASSERT( m_collision == 0 );
	ASSERT(	m_collision_object == 0 );
	ASSERT(	m_statics_shell	== 0 );
	m_collision						= &*collision::create_triangle_mesh_geometry( g_allocator, mesh_resource );
	m_collision_object				= &*collision::create_geometry_object( g_allocator, object_type_geometry, float4x4().identity(), m_collision );

	collision::geometry_instance*	gi =  dynamic_cast<collision::geometry_instance*>(m_collision_object);
	m_statics_shell					= &(*w.create_shell_static	( gi ));//static_cast_checked<collision::geometry_instance*,collision::object*>( s_collision_object )

	w.set_ready				( true );
}

void	collision_static::destroy( physics::world &w )
{
	if ( m_collision_object )
		collision::destroy	( m_collision_object );

	if( m_statics_shell )
		w.destroy_shell( m_statics_shell );

	if ( m_collision )
		collision::destroy	( m_collision );

}

}