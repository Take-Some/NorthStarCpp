////////////////////////////////////////////////////////////////////////////
//	Created		: 24.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_COLLISION_COMMON_TYPES_H_INCLUDED
#define NORTHSTARCPP_COLLISION_COMMON_TYPES_H_INCLUDED

namespace northstarcpp {
namespace render {
namespace debug {
	struct renderer;
} // namespace debug
} // namespace render

namespace collision {

class object;

struct triangle_result {
	object const*	object;
	u32				triangle_id;

	inline triangle_result	( collision::object const* object, u32 const triangle_id ) :
		object		( object ),
		triangle_id	( triangle_id )
	{
	}
}; // struct triangle_result

typedef object const*														object_result;

struct ray_triangle_result {
	object const*	object;
	u32				triangle_id;
	float			distance;

	inline ray_triangle_result	( collision::object const* object, u32 const triangle_id, float const distance ) :
		object		( object ),
		triangle_id	( triangle_id ),
		distance	( distance )
	{
	}
}; // struct triangle_result

struct ray_object_result {
	object const*	object;
	float			distance;

	inline ray_object_result	( collision::object const* object, float const distance ) :
		object		( object ),
		distance	( distance )
	{
	}
}; // struct object_result

typedef vectora< object const* >											objects_type;
typedef vectora< triangle_result >											triangles_type;
typedef vectora< ray_object_result >										ray_objects_type;
typedef vectora< ray_triangle_result >										ray_triangles_type;

typedef fastdelegate::FastDelegate< bool ( ray_object_result const& ) >		objects_predicate_type;
typedef fastdelegate::FastDelegate< bool ( ray_triangle_result const& ) >	triangles_predicate_type;

typedef u32																	object_type;
typedef u32																	query_type;

typedef memory::base_allocator*												allocator_type;

} // namespace collision
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_COLLISION_COMMON_TYPES_H_INCLUDED