////////////////////////////////////////////////////////////////////////////
//	Created		: 14.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BENCHMARK_RENDERER_H_INCLUDED
#define BENCHMARK_RENDERER_H_INCLUDED


namespace northstarcpp {
namespace graphics_benchmark {

class gpu_timer;

class benchmark_renderer: public boost::noncopyable
{
public:
	
	struct colored_vertex
	{
		math::float3	position;
		u32				color;
		math::float2	uv;
	}; 
	
	struct mesh
	{
		u32						ioffset;
		u32						voffset;
		u32						index_count;
	};
	
	typedef northstarcpp::render::vector< colored_vertex >	colored_vertices_type;
	typedef northstarcpp::render::vector< u16 >				colored_indices_type;
	
	benchmark_renderer			( );
	~benchmark_renderer			( );
	
	void render_fullscreen_quad	( gpu_timer& gtimer);

	void prepare_quad();
	u32  cook_mesh				( colored_vertices_type const& vertices, colored_indices_type  const& indices);
	void render_cooked_mesh		( u32 mesh_index, northstarcpp::math::float4x4 const& wvp_matrix = northstarcpp::math::float4x4().identity());
	void render_3d_sphere		( gpu_timer& gtimer, northstarcpp::math::float4x4 const& wvp_matrix);
	
private:
	u32									m_quad_mesh_idx;
	u32									m_3d_sphere_mesh_idx;

	std::vector<mesh>					m_meshes;

	northstarcpp::render_dx10::ref_geometry		m_geom;

	northstarcpp::render_dx10::constant_host*	m_WVP;
}; // class benchmark_renderer

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // BENCHMARK_RENDERER_H_INCLUDED