////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/collision/api.h>
#include "triangle_mesh_buffer.h"
#include "triangle_mesh_resource.h"
#include "sphere_geometry.h"
#include "box_geometry.h"
#include "cylinder_geometry.h"
#include "loose_oct_tree.h"
#include "geometry_object.h"
#include "geometry_instance.h"
#include "sphere_geometry.h"
#include "compound_geometry.h"

using northstarcpp::collision::detail::world;
using northstarcpp::collision::triangle_mesh_buffer;
using northstarcpp::collision::triangle_mesh_resource;
using northstarcpp::collision::loose_oct_tree;
using northstarcpp::collision::object;
using northstarcpp::non_null;
using northstarcpp::math::float3;
using northstarcpp::math::float4x4;

namespace northstarcpp {
namespace collision {

non_null<northstarcpp::collision::geometry>::ptr create_triangle_mesh_geometry	(
		memory::base_allocator* allocator,
		float3 const* vertices,
		u32 vertex_count,
		u32 const* indices,
		u32 index_count,
		u32 const* triangle_data,
		u32 triangle_count
	)
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, triangle_mesh_buffer ) ( allocator, vertices, vertex_count, indices, index_count, triangle_data, triangle_count );
}

non_null<northstarcpp::collision::geometry>::ptr create_triangle_mesh_geometry	(
	memory::base_allocator* allocator,
	float3 const* vertices,
	u32 vertex_count,
	u32 const* indices,
	u32 index_count
	)
{
	return		create_triangle_mesh_geometry( allocator, vertices, vertex_count, indices, index_count, 0, 0 );
}

non_null<northstarcpp::collision::geometry>::ptr create_triangle_mesh_geometry	( memory::base_allocator* allocator, northstarcpp::resources::managed_resource_ptr const resource )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, triangle_mesh_resource ) ( allocator, resource );
}

non_null<northstarcpp::collision::geometry>::ptr create_sphere_geometry	( memory::base_allocator* allocator, float radius )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, sphere_geometry ) ( allocator, radius );
}

non_null<northstarcpp::collision::geometry>::ptr create_box_geometry( memory::base_allocator* allocator, const northstarcpp::math::float3 &half_sides )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, box_geometry ) ( allocator, half_sides );
}

non_null<northstarcpp::collision::geometry>::ptr create_cylinder_geometry( memory::base_allocator* allocator, float radius, float half_length )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, cylinder_geometry ) ( allocator, radius, half_length );
}

non_null<geometry>::ptr	create_compound_geometry		( memory::base_allocator* allocator, vectora<collision::geometry_instance*> &instances )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, compound_geometry ) ( allocator, instances );
}
void destroy		( northstarcpp::collision::geometry* geometry )
{
	if (!geometry)
		return;

	NORTHSTARCPP_DELETE_IMPL( &geometry->get_allocator(), geometry );
}

non_null<northstarcpp::collision::space_partitioning_tree>::ptr create_space_partitioning_tree( memory::base_allocator* allocator, float min_aabb_radius, u32 reserve_node_count )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, loose_oct_tree )( allocator, min_aabb_radius, reserve_node_count );
}

void destroy						( northstarcpp::collision::space_partitioning_tree* tree )
{
	if (!tree)
		return;

	loose_oct_tree* temp = static_cast_checked<loose_oct_tree*>(tree);
	NORTHSTARCPP_DELETE_IMPL( &temp->get_allocator(), temp );
}

non_null<northstarcpp::collision::object>::ptr create_geometry_object	( memory::base_allocator* allocator, object_type const object_type, float4x4 const& matrix, non_null<northstarcpp::collision::geometry const>::ptr geometry )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, detail::geometry_object ) ( allocator, object_type, matrix,  geometry );
}

void destroy				( object* object )
{
	if (!object)
		return;

	detail::geometry_object* temp		= static_cast_checked<detail::geometry_object*>(object);
	memory::base_allocator* allocator	= &temp->get_allocator();
	NORTHSTARCPP_DELETE_IMPL					( allocator, temp );
}

non_null<northstarcpp::collision::geometry_instance>::ptr create_geometry_instance	( memory::base_allocator* allocator, float4x4 const& matrix, non_null<northstarcpp::collision::geometry const>::ptr geometry )
{
	return		NORTHSTARCPP_NEW_IMPL( allocator, detail::geometry_instance ) ( allocator, matrix,  geometry );
}

void destroy				( northstarcpp::collision::geometry_instance* object )
{
	if (!object)
		return;

	detail::geometry_instance* temp = static_cast_checked<detail::geometry_instance*>(object);
	NORTHSTARCPP_DELETE_IMPL( &temp->get_allocator(), temp );
}

} // namespace collision
} // namespace northstarcpp