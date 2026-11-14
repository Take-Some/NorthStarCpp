////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "editor_collision_object.h"

using northstarcpp::math::float3;
using northstarcpp::math::float4x4;

#include <northstarcpp/render/world.h>
#include <northstarcpp/render/base/debug_renderer.h>
#include <northstarcpp/collision/geometry.h>

using northstarcpp::editor::detail::editor_colision_object;
using northstarcpp::editor::transform_control;
using northstarcpp::editor::enum_transform_axis;

editor_colision_object::editor_colision_object	( float4x4 const& matrix, 
												 northstarcpp::collision::geometry const* geometry, 
												 transform_control* owner_control, 
												 enum_transform_axis axis ):
m_geometry			( geometry ),
m_owner				( owner_control ),
m_transform_original( matrix ),
m_axis				( axis)
{	
	set_matrix( float4x4().identity() );
}

editor_colision_object::~editor_colision_object	( )
{

}


transform_control*	editor_colision_object::get_owner			( ) const
{
	return m_owner;
}

void editor_colision_object::render	( northstarcpp::render::base::world& world ) const
{
	m_geometry->render( world, m_transform );
	//world.debug().draw_aabb( m_transform.transform_position( get_center() ) , get_radius(), 0xFFFFFF );
}

bool editor_colision_object::aabb_query		( northstarcpp::collision::query_type query_type, northstarcpp::math::aabb const& aabb, northstarcpp::collision::Results& results )  const
{
	return m_geometry->prismoid_query( this, math::prismoid( aabb, m_transform_invert ), results );
}
bool editor_colision_object::frustum_query	( northstarcpp::collision::query_type query_type, northstarcpp::math::frustum const& frustum, northstarcpp::collision::Results& results )  const
{
	return m_geometry->prismoid_query( this, math::prismoid( frustum, m_transform_invert ), results );
}
bool editor_colision_object::ray_query		( northstarcpp::collision::query_type query_type, northstarcpp::math::float3 const& origin, northstarcpp::math::float3 const& direction, float max_distance, float& distance, northstarcpp::collision::Results& results, northstarcpp::collision::Predicate const& predicate ) const
{
	float3 const new_origin		= m_transform_invert.transform_position(origin);
	float3 const new_direction	= m_transform_invert.transform_direction(direction);
	m_geometry->ray_query		( this, new_origin, new_direction, max_distance, distance, results, predicate );
	return						false;
}

bool editor_colision_object::aabb_test		( northstarcpp::collision::query_type query_type, northstarcpp::math::aabb const& aabb ) const
{
	return true;
}
bool editor_colision_object::frustum_test	( northstarcpp::collision::query_type query_type, northstarcpp::math::frustum const& frustum ) const
{
	return true;
}
bool editor_colision_object::ray_test		( northstarcpp::collision::query_type query_type, northstarcpp::math::float3 const& origin, northstarcpp::math::float3 const& direction, float max_distance ) const
{
	return true;
}

northstarcpp::collision::geometry const* editor_colision_object::get_object_geometry	( ) const
{
	return m_geometry;	
}

northstarcpp::non_null<northstarcpp::collision::geometry const>::ptr	editor_colision_object::get_geometry	( ) const
{
	return m_geometry;
}

void editor_colision_object::set_matrix		( northstarcpp::math::float4x4 const& matrix )
{
	m_transform_general = matrix;
	m_transform = m_transform_original*m_transform_general;
	m_transform_invert = math::invert4x3( m_transform );

	m_transformed_aabb = m_geometry->get_aabb();
	modify( m_transformed_aabb, m_transform );
}

void editor_colision_object::modify									( northstarcpp::math::aabb& aabb, float4x4 const& transform )
{
	float3 const	double_size = aabb.max - aabb.min;
	float3 const vx	= transform.i.xyz() * double_size.x;
	float3 const vy = transform.j.xyz() * double_size.y;
	float3 const vz = transform.k.xyz() * double_size.z;

	float3 tmp;
	tmp = aabb.max = aabb.min	= aabb.min*transform;

	aabb.min.min( tmp+vx );
	aabb.min.min( tmp+vy );
	aabb.min.min( tmp+vz );
	aabb.min.min( tmp+vx+vy+vz );

	aabb.max.max( tmp+vx );
	aabb.max.max( tmp+vy );
	aabb.max.max( tmp+vz );
	aabb.max.max( tmp+vx+vy+vz );
}

northstarcpp::math::float4x4 const& editor_colision_object::get_matrix		( ) const
{
	return m_transform;
}

void editor_colision_object::set_matrix_original		( northstarcpp::math::float4x4 const& matrix )
{
	m_transform_original = matrix;
	set_matrix( m_transform_general );
}

northstarcpp::math::float4x4 const& editor_colision_object::get_matrix_original	( ) const
{
	return m_transform_original;
}

northstarcpp::math::float3		editor_colision_object::get_center			( ) const
{
	return m_transformed_aabb.center();
}

northstarcpp::math::float3		editor_colision_object::get_radius			( ) const
{
	return m_transformed_aabb.size();
}

enum_transform_axis		editor_colision_object::get_axis			( ) const
{
	return m_axis;
}

void					editor_colision_object::set_priority		( u32 priority ) 
{
	m_priority = priority;
}

u32 					editor_colision_object::get_priority		( ) const
{
	return m_priority;
}



// bool editor_colision_object::aabb_query		( object const* object, northstarcpp::math::aabb const& aabb, northstarcpp::collision::Results& results ) const
// {
// 	return true;
// }
// bool editor_colision_object::frustum_query	( object const* object, northstarcpp::math::frustum const& frustum, northstarcpp::collision::Results& results ) const
// {
// 	return true;
// }
// bool editor_colision_object::ray_query		( object const* object, northstarcpp::math::float3 const& origin, northstarcpp::math::float3 const& direction, float max_distance, float& distance, northstarcpp::collision::Results& results, northstarcpp::collision::Predicate const& predicate ) const
// {
// 	float3 const new_origin		= m_transform.transform_position(origin);
// 	float3 const new_direction	= m_transform.transform_direction(direction);
// 	return		m_geometry->ray_query( this, new_origin, new_direction, max_distance, distance, results, predicate );
// }
