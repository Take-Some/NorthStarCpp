////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GEOMETRY_PRIMITIVES_H_INCLUDED
#define GEOMETRY_PRIMITIVES_H_INCLUDED

namespace northstarcpp {
template <typename T> class vectora;
namespace geometry_utils{

////////////////////////////////////////////////////////////////////////////
// Oriented Bounding Box
////////////////////////////////////////////////////////////////////////////

namespace obb {

	extern NORTHSTARCPP_CORE_API const u32	vertex_count;
	extern NORTHSTARCPP_CORE_API const float	vertices[];

	extern NORTHSTARCPP_CORE_API const u32	pair_count;
	extern NORTHSTARCPP_CORE_API const u16	pairs[];

} // namespace obb

////////////////////////////////////////////////////////////////////////////
// Rectangle
////////////////////////////////////////////////////////////////////////////
namespace rectangle {

	extern NORTHSTARCPP_CORE_API const u32	vertex_count;
	extern NORTHSTARCPP_CORE_API const float	vertices[];

	extern NORTHSTARCPP_CORE_API const u32	pair_count;
	extern NORTHSTARCPP_CORE_API const u16	pairs[];

} // namespace Rectangle

////////////////////////////////////////////////////////////////////////////
// Ellipsoid
////////////////////////////////////////////////////////////////////////////
namespace ellipsoid {
	extern NORTHSTARCPP_CORE_API const u32	vertex_count;
	extern NORTHSTARCPP_CORE_API const float	vertices[ ];

	extern NORTHSTARCPP_CORE_API const u32	pair_count;
	extern NORTHSTARCPP_CORE_API const u16	pairs[];
} // namespace ellipsoid

////////////////////////////////////////////////////////////////////////////
// Cone
////////////////////////////////////////////////////////////////////////////
namespace cone {
	extern NORTHSTARCPP_CORE_API const u32	vertex_count;
	extern NORTHSTARCPP_CORE_API const float	vertices[];

	extern NORTHSTARCPP_CORE_API const u32	pair_count;
	extern NORTHSTARCPP_CORE_API const u16	pairs[];
} // namespace cone


////////////////////////////////////////////////////////////////////////////
// Ellipse 
////////////////////////////////////////////////////////////////////////////
namespace ellipse {
	extern NORTHSTARCPP_CORE_API const u32	vertex_count;
	extern NORTHSTARCPP_CORE_API const float	vertices[];

	extern NORTHSTARCPP_CORE_API const u32	pair_count;
	extern NORTHSTARCPP_CORE_API const u16	pairs[];
} // namespace ellipse

////////////////////////////////////////////////////////////////////////////
// Cube solid
////////////////////////////////////////////////////////////////////////////
namespace cube_solid {
	extern NORTHSTARCPP_CORE_API const u32 vertex_count;
	extern NORTHSTARCPP_CORE_API const float vertices[];

	extern NORTHSTARCPP_CORE_API const u32 face_count; 
	extern NORTHSTARCPP_CORE_API const u32 index_count; 
	extern NORTHSTARCPP_CORE_API const u16 faces[];
} //namespace cube_solid 


////////////////////////////////////////////////////////////////////////////
// Rectangle solid
////////////////////////////////////////////////////////////////////////////
namespace rectangle_solid {

	extern NORTHSTARCPP_CORE_API const u32 vertex_count;
	extern NORTHSTARCPP_CORE_API const float vertices[];

	extern NORTHSTARCPP_CORE_API const u32 face_count;
	extern NORTHSTARCPP_CORE_API const u32 index_count; 
	extern NORTHSTARCPP_CORE_API const u16 faces[];

} // namespace Rectangle

////////////////////////////////////////////////////////////////////////////
// Cone solid
////////////////////////////////////////////////////////////////////////////
namespace cone_solid {
	extern NORTHSTARCPP_CORE_API const u32 vertex_count;
	extern NORTHSTARCPP_CORE_API const float vertices[];

	extern NORTHSTARCPP_CORE_API const u32 face_count; 
	extern NORTHSTARCPP_CORE_API const u32 index_count; 
	extern NORTHSTARCPP_CORE_API const u16 faces[];
} // namespace cone_solid


////////////////////////////////////////////////////////////////////////////
// Cylinder
////////////////////////////////////////////////////////////////////////////
namespace cylinder_solid {
	extern NORTHSTARCPP_CORE_API const u32 vertex_count;
	extern NORTHSTARCPP_CORE_API const float vertices[];

	extern NORTHSTARCPP_CORE_API const u32 face_count; 
	extern NORTHSTARCPP_CORE_API const u32 index_count; 
	extern NORTHSTARCPP_CORE_API const u16 faces[];
} //namespace cylinder_solid 

////////////////////////////////////////////////////////////////////////////
// Ellipsoid solid
////////////////////////////////////////////////////////////////////////////
namespace ellipsoid_solid {
	extern NORTHSTARCPP_CORE_API const u32 vertex_count;
	extern NORTHSTARCPP_CORE_API const float vertices[];

	extern NORTHSTARCPP_CORE_API const u32 face_count; 
	extern NORTHSTARCPP_CORE_API const u32 index_count; 
	extern NORTHSTARCPP_CORE_API const u16 faces[];
} //namespace ellipsoid_solid 

bool generate_torus			( northstarcpp::vectora< math::float3 >& vertices, northstarcpp::vectora< u16 >& indices, math::float4x4 transform,
									 float outer_raduius,  float inner_raduius, u16 outer_segments, u16 inner_segments );

} // namespace geometry_utils
} // namespace northstarcpp

#endif // #ifndef GEOMETRY_PRIMITIVES_H_INCLUDED