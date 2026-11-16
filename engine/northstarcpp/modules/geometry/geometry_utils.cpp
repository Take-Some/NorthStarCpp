////////////////////////////////////////////////////////////////////////////
//	Created		: 09.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "../core/pch.h"
#include <cmath>
#include <limits>
#include <initializer_list>
#include <northstarcpp/geometry_utils.h>
#include <northstarcpp/geometry_primitives.h>
#include <northstarcpp/linkage_helper.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
	DECLARE_LINKAGE_ID(core_geometry_utils)
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

using northstarcpp::math::float4x4;
using northstarcpp::math::float3;

namespace northstarcpp {
namespace geometry_utils{

void create_primitive(	geom_vertices_type& avertices, 
						 geom_indices_type& aindices, 
						 float4x4 transform, 
						 float const* vertices,  
						 u32 vertex_count, 
						 u16 const* faces, 
						 u32 index_count )
{
	float3 tmp_vertex;
	avertices.resize( vertex_count );
	for( u32 i = 0, j = 0 ; i < vertex_count; ++i, j+=3 )
	{
		tmp_vertex.x = vertices[j];
		tmp_vertex.y = vertices[j+1];
		tmp_vertex.z = vertices[j+2];

		avertices[i] = transform.transform_position( tmp_vertex );
	}

	aindices.resize( index_count );
	for( u32 i = 0; i < index_count; ++i)
		aindices[i] = faces[i];
}

//bool create_torus( debug_vertices_type& vertices, debug_indices_type& indices, float outer_raduius,  float inner_raduius, u16 outer_segments, u16 inner_segments, color color )
// {
//	northstarcpp::vectora< float3 > tmp_vertices ( *g_allocator );
//	northstarcpp::vectora< u16 > tmp_indices ( *g_allocator );
//
//	bool result = create_torus	( tmp_vertices, tmp_indices, float4x4().identity(), outer_raduius,  inner_raduius, outer_segments, inner_segments );
//
//	vertices.resize( tmp_vertices.size() );
//	for ( u32 i = 0; i < tmp_vertices.size(); ++i)
//	{
//		vertices[i].position = tmp_vertices[i];
//		vertices[i].color = color;
//	}
//
//	indices.resize( tmp_indices.size() );
//	for ( u32 i = 0; i < tmp_indices.size(); ++i)
//		indices[i] = tmp_indices[i];
//
//	return result;
// }

 bool create_torus( geom_vertices_type& vertices, 
					geom_indices_type& indices, 
					float4x4 transform,
					float outer_raduius,  
					float inner_raduius, 
					u16 outer_segments, 
					u16 inner_segments )
{
#ifdef NORTHSTARCPP_STANDALONE_MATH
    // The standalone runtime uses std::vector, while the original
    // editor-era generator takes allocator-dependent vectora<T>.
    // Preserve the legacy ABI in its own build and expose an equivalent
    // C++17 implementation for the actual NorthStarCpp runtime.
    if (outer_segments < 3 || inner_segments < 3 ||
        outer_segments > 4096 || inner_segments > 4096 ||
        !(outer_raduius > 0.f) || !(inner_raduius > 0.f))
        return false;
    auto const count = static_cast<std::size_t>(outer_segments) * inner_segments;
    if (count > std::numeric_limits<u16>::max()) return false;
    vertices.resize(count);
    indices.clear();
    indices.reserve(static_cast<std::size_t>(outer_segments) *
                    inner_segments * 6u);
    constexpr float tau = 6.2831853071795864769f;
    for (unsigned ring=0; ring<inner_segments; ++ring) {
        float const theta=tau*float(ring)/float(inner_segments);
        float const st=std::sin(theta),ct=std::cos(theta);
        for (unsigned segment=0; segment<outer_segments; ++segment) {
            float const phi=tau*float(segment)/float(outer_segments);
            float const sp=std::sin(phi),cp=std::cos(phi);
            float const radius=outer_raduius+inner_raduius*ct;
            vertices[ring*outer_segments+segment]=transform.transform_position(
                float3(radius*cp,inner_raduius*st,radius*sp));
        }
    }
    for (unsigned ring=0;ring<inner_segments;++ring)
        for (unsigned segment=0;segment<outer_segments;++segment) {
            auto const a=ring*outer_segments+segment;
            auto const b=ring*outer_segments+(segment+1)%outer_segments;
            auto const c=((ring+1)%inner_segments)*outer_segments+segment;
            auto const d=((ring+1)%inner_segments)*outer_segments+
                (segment+1)%outer_segments;
            for(auto const id:{a,c,b,b,c,d})
                indices.push_back(static_cast<u16>(id));
        }
    return true;
#else
    return generate_torus(vertices, indices, transform, outer_raduius,
                          inner_raduius, outer_segments, inner_segments);
#endif
}

bool create_cylinder( geom_vertices_type& vertices, geom_indices_type& indices, float4x4 transform, float3 size )
{
	 create_primitive	( vertices, 
							indices, 
							create_scale(size)*transform, 
							cylinder_solid::vertices,  
							cylinder_solid::vertex_count, 
							cylinder_solid::faces, 
							cylinder_solid::index_count );
	 return true;
}

bool create_cone( geom_vertices_type& vertices, geom_indices_type& indices, float4x4 transform, float3 size )
{
	create_primitive	( vertices, 
		indices, 
		create_scale(size)*transform, 
		cone_solid::vertices,  
		cone_solid::vertex_count, 
		cone_solid::faces, 
		cone_solid::index_count );
	return true;
}


bool create_cube( geom_vertices_type& vertices, geom_indices_type& indices, float4x4 transform, float3 size )
{
	create_primitive	( vertices, 
		indices, 
		create_scale(size)*transform, 
		cube_solid::vertices,  
		cube_solid::vertex_count, 
		cube_solid::faces, 
		cube_solid::index_count );
	return true;
}

bool create_ellipsoid( geom_vertices_type& vertices, geom_indices_type& indices, float4x4 transform, float3 size )
{
	create_primitive	( vertices, 
		indices, 
		create_scale(size)*transform, 
		ellipsoid_solid::vertices,  
		ellipsoid_solid::vertex_count, 
		ellipsoid_solid::faces, 
		ellipsoid_solid::index_count );

	return true;
}

bool create_ring( geom_vertices_type& vertices, 
					geom_indices_type& indices, 
					float inner_radius, 
					float width,
					u16 segments_count)
{
	ASSERT					(segments_count>3);
	
	vertices.resize			(segments_count*2);
	indices.resize			(segments_count*6);

	float segment_ang		= math::pi_x2/segments_count;
	u16 i					= 0;
	u32 vidx				= 0;
	float outer_radius		= inner_radius+width;
	for(i=0; i<segments_count;++i)
	{
		math::sine_cosine	sincos(segment_ang*i);
		vertices[vidx++].set(inner_radius*sincos.cosine, 0.0f, -inner_radius*sincos.sine);
		vertices[vidx++].set(outer_radius*sincos.cosine, 0.0f, -outer_radius*sincos.sine);
	}

	u32 iidx				= 0;
	for(i=0; i<segments_count-1; ++i)
	{
		indices[iidx++]		= (i*2);
		indices[iidx++]		= (i*2+1);
		indices[iidx++]		= (i*2+3);

		indices[iidx++]		= (i*2);
		indices[iidx++]		= (i*2+3);
		indices[iidx++]		= (i*2+2);
	}
	{ // last segment
		indices[iidx++]		= (i*2);
		indices[iidx++]		= (i*2+1);
		indices[iidx++]		= (1);

		indices[iidx++]		= (i*2);
		indices[iidx++]		= (1);
		indices[iidx++]		= (0);
	}
	return true;
}

} //namespace geometry_utils
} //namespace northstarcpp
