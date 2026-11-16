////////////////////////////////////////////////////////////////////////////
//	Created		: 09.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GEOMETRY_UTILS_H_INCLUDED
#define GEOMETRY_UTILS_H_INCLUDED
#ifdef NORTHSTARCPP_STANDALONE_MATH
#   include <vector>
#endif

namespace northstarcpp {
namespace geometry_utils{

#ifdef NORTHSTARCPP_STANDALONE_MATH
typedef std::vector<math::float3> geom_vertices_type;
typedef std::vector<u16> geom_indices_type;
#else
typedef northstarcpp::vectora< math::float3 > geom_vertices_type;
typedef northstarcpp::vectora< u16 > geom_indices_type;
#endif

//NORTHSTARCPP_CORE_API bool		create_torus	( debug_vertices_type& vertices, debug_indices_type& indices, 		
//												float outer_raduius,  float inner_raduius, u16 outer_segments, u16 inner_segments, color color );
NORTHSTARCPP_CORE_API bool		create_torus	( geom_vertices_type& vertices, geom_indices_type& indices, math::float4x4 transform, 
												float outer_raduius,  float inner_raduius, u16 outer_segments, u16 inner_segments );

NORTHSTARCPP_CORE_API bool		create_cone		( geom_vertices_type& vertices, geom_indices_type& indices, math::float4x4 transform, math::float3 size );

NORTHSTARCPP_CORE_API bool		create_cylinder	( geom_vertices_type& vertices, geom_indices_type& indices, math::float4x4 transform, math::float3 size );

NORTHSTARCPP_CORE_API bool		create_cube		( geom_vertices_type& vertices, geom_indices_type& indices, math::float4x4 transform, math::float3 size );

NORTHSTARCPP_CORE_API bool		create_ellipsoid( geom_vertices_type& vertices, geom_indices_type& indices, math::float4x4 transform, math::float3 size );

NORTHSTARCPP_CORE_API bool		create_ring		( geom_vertices_type& vertices, geom_indices_type& indices, float inner_radius, float width, u16 segments_count );


} // namespace geometry
} // namespace northstarcpp

#endif // GEOMETRY_UTILS_H_INCLUDED
