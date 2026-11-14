////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "collision_object.h"

using ::northstarcpp::math::float3;
using ::northstarcpp::math::float4x4;

#pragma unmanaged
#include <northstarcpp/compat/render/world.h>
#include <northstarcpp/compat/render/base/debug_renderer.h>
#include <northstarcpp/compat/collision/geometry.h>
#include <northstarcpp/compat/collision/api.h>
#pragma managed

#include "picker.h"

using namespace northstarcpp;
using northstarcpp::editor::collision_object;

collision_object::collision_object	( ::northstarcpp::collision::geometry const* geometry ) :
	m_priority			( collision_objects_priority_dynamics ),
	m_geometry			( &*::northstarcpp::collision::create_geometry_instance( g_allocator, float4x4().identity(), geometry) )
{
//	set_type			(collision_type_base);
}

collision_object::~collision_object	( )
{
	northstarcpp::collision::geometry_instance* geometry = m_geometry;
	::northstarcpp::collision::destroy	( geometry );
}

void collision_object::render			( render::debug::renderer& renderer ) const
{
	m_geometry->render( renderer );
	//renderer.draw_aabb( m_transform.transform_position( get_center() ) , get_radius(), 0xFFFFFF );
}

bool collision_object::aabb_query		( northstarcpp::math::aabb const& aabb, northstarcpp::collision::triangles_type& results )  const
{
	return m_geometry->aabb_query( this, aabb, results );
}

bool collision_object::cuboid_query		( northstarcpp::math::cuboid const& cuboid, northstarcpp::collision::triangles_type& results )  const
{
	return m_geometry->cuboid_query( this, cuboid, results );
}

bool collision_object::ray_query		( northstarcpp::math::float3 const& origin, northstarcpp::math::float3 const& direction, float max_distance, float& distance, northstarcpp::collision::ray_triangles_type& results, northstarcpp::collision::triangles_predicate_type const& predicate ) const
{
	return m_geometry->ray_query( this, origin, direction, max_distance, distance, results, predicate );
}

bool collision_object::aabb_test		( northstarcpp::math::aabb const& aabb ) const
{
	return m_geometry->aabb_test( aabb );
}

bool collision_object::cuboid_test		( northstarcpp::math::cuboid const& cuboid ) const
{
	return m_geometry->cuboid_test( cuboid );
}

bool collision_object::ray_test			( northstarcpp::math::float3 const& origin, northstarcpp::math::float3 const& direction, float max_distance, float& distance ) const
{
	return m_geometry->ray_test( origin, direction, max_distance, distance );
}

void collision_object::add_triangles	( collision::triangles_type& triangles ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( triangles );
	UNREACHABLE_CODE();
}

northstarcpp::collision::geometry const* collision_object::get_object_geometry	( ) const
{
	return &*m_geometry->get_geometry();	
}

northstarcpp::non_null<northstarcpp::collision::geometry const>::ptr	collision_object::get_geometry	( ) const
{
	return m_geometry->get_geometry();
}

void collision_object::set_matrix		( northstarcpp::math::float4x4 const& matrix )
{
	m_geometry->set_matrix( matrix );
}

northstarcpp::math::float4x4 const& collision_object::get_matrix		( ) const
{
	return m_geometry->get_matrix();
}

northstarcpp::math::float3		collision_object::get_center			( ) const
{
	return get_aabb().center();
}

northstarcpp::math::float3		collision_object::get_extents			( ) const
{
	return get_aabb().extents();
}

void					collision_object::set_priority		( u32 priority ) 
{
	m_priority = priority;
}

u32 					collision_object::get_priority		( ) const
{
	return m_priority;
}

//enum_collision_object_type		collision_object::get_type	( ) const
//{
//	return enum_collision_object_type_base;
//}

math::aabb const& collision_object::get_aabb				( ) const
{
	return m_geometry->get_aabb();
}
