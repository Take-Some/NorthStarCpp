////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_COLLISION_API_H_INCLUDED
#define NORTHSTARCPP_COLLISION_API_H_INCLUDED

#ifndef NORTHSTARCPP_COLLISION_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_COLLISION_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_COLLISION_BUILDING
#			define NORTHSTARCPP_COLLISION_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_COLLISION_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_COLLISION_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_COLLISION_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_COLLISION_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_COLLISION_API

#include <northstarcpp/collision/common_types.h>			// for object_type

namespace northstarcpp {
namespace collision {

struct space_partitioning_tree;
struct geometry_instance;
struct geometry;
class object;

NORTHSTARCPP_COLLISION_API	non_null<space_partitioning_tree>::ptr create_space_partitioning_tree ( memory::base_allocator* allocator, float min_aabb_radius, u32 reserve_node_count );
NORTHSTARCPP_COLLISION_API	void							destroy							( space_partitioning_tree* tree );

NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_triangle_mesh_geometry	(
														memory::base_allocator* allocator, 
														math::float3 const* vertices,
														u32 vertex_count,
														u32 const* indices,
														u32 index_count,
														u32 const* triangle_data,
														u32 triangle_count
													);
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_triangle_mesh_geometry	(
														memory::base_allocator* allocator, 
														math::float3 const* vertices,
														u32 vertex_count,
														u32 const* indices,
														u32 index_count
													);
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_triangle_mesh_geometry	( memory::base_allocator* allocator, resources::managed_resource_ptr resource );
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_sphere_geometry			( memory::base_allocator* allocator, float radius );
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_box_geometry				( memory::base_allocator* allocator, const northstarcpp::math::float3 &half_sides );
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_cylinder_geometry		( memory::base_allocator* allocator, float radius, float half_length );
NORTHSTARCPP_COLLISION_API	non_null<geometry>::ptr			create_compound_geometry		( memory::base_allocator* allocator, vectora<collision::geometry_instance*> &instances );
NORTHSTARCPP_COLLISION_API	void							destroy							( geometry* geometry );

NORTHSTARCPP_COLLISION_API	non_null<object>::ptr			create_geometry_object			( memory::base_allocator* allocator, object_type object_type, math::float4x4 const& matrix, non_null<geometry const>::ptr geometry );
NORTHSTARCPP_COLLISION_API	void							destroy							( object* object );

NORTHSTARCPP_COLLISION_API	non_null<geometry_instance>::ptr create_geometry_instance		( memory::base_allocator* allocator, math::float4x4 const& matrix, non_null<geometry const>::ptr geometry );
NORTHSTARCPP_COLLISION_API	void							destroy							( geometry_instance* object );


} // namespace collision
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_COLLISION_API_H_INCLUDED