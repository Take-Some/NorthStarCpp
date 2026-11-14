////////////////////////////////////////////////////////////////////////////
//	Created		: 13.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GEOMETRY_OBJECT_H_INCLUDED
#define GEOMETRY_OBJECT_H_INCLUDED

#include <northstarcpp/compat/collision/object.h>
#include "geometry_instance.h"

namespace northstarcpp {
namespace render {
namespace debug {
	struct renderer;
} // namespace debug
} // namespace render

namespace collision {
namespace detail {

class world;

class geometry_object :	public detail::geometry_instance, public object {
public:
								geometry_object	( memory::base_allocator* allocator, object_type const object_type, float4x4 const& matrix, non_null<collision::geometry const>::ptr geometry );
	virtual						~geometry_object( );

public:
	virtual	void				render			( render::debug::renderer& renderer ) const;

	virtual	bool				aabb_query		( math::aabb const& aabb, triangles_type& triangles ) const;
	virtual	bool				cuboid_query	( math::cuboid const& cuboid, triangles_type& triangles ) const;
	virtual	bool				ray_query		(
									float3 const& origin,
									float3 const& direction,
									float max_distance,
									float& distance,
									ray_triangles_type& triangles,
									triangles_predicate_type const& predicate
								) const;

	virtual	bool				aabb_test		( math::aabb const& aabb ) const;
	virtual	bool				cuboid_test		( math::cuboid const& cuboid ) const;
	virtual	bool				ray_test		( float3 const& origin, float3 const& direction, float max_distance, float& distance ) const;

	virtual	void				add_triangles	( triangles_type& triangles ) const;

private:
	typedef geometry_instance	super;

}; // class geometry_object

} // namespace detail
} // namespace collision
} // namespace northstarcpp

#endif // #ifndef GEOMETRY_OBJECT_H_INCLUDED