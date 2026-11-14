////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "platform_ps3.h"

using northstarcpp::render::engine::wrapper;
using northstarcpp::math::float4x4;
using northstarcpp::math::int2_pod;
using northstarcpp::math::rectangle;

namespace northstarcpp {
namespace render {
namespace ps3 {

render::platform * create_render_platform( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( window_handle );

	return NEW(platform)( wrapper );
}

platform::platform							( engine::wrapper& engine ) :
	m_frame_id	( 0 )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( engine );
}

platform::~platform							( )
{
}

void platform::clear_resources					( )
{
}

void platform::set_view_matrix					( float4x4 const& view )
{
	m_view_matrix	= view;
}

void platform::set_projection_matrix			( float4x4 const& projection )
{
	m_projection_matrix	= projection;
}

float4x4 const& platform::get_view_matrix		( ) const
{
	return		m_view_matrix;
}

float4x4 const& platform::get_projection_matrix( ) const
{
	return		m_projection_matrix;
}

rectangle<int2_pod> platform::get_viewport		( ) const
{
	return		m_viewport;
}

void platform::draw_debug_lines				( vector< vertex_colored > const& vertices, vector< u16 > const& indices )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &vertices, &indices );
}

void platform::draw_debug_triangles			( vector< vertex_colored > const& vertices, vector< u16 > const& indices )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &vertices, &indices );
}

void platform::draw_ui_vertices				( ui::vertex const * vertices, u32 const & count, int prim_type, int point_type )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( vertices, count, prim_type, point_type );
}

void platform::clear_zbuffer					( float z_value )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( z_value );
}

void platform::draw_frame					( )
{
	++m_frame_id;
}

u32 platform::frame_id							( )
{
	return		m_frame_id;
}

void platform::add_static					( resources::managed_resource_ptr const& collision, resources::managed_resource_ptr const& geometry, resources::managed_resource_ptr const& visuals )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &collision, &geometry, &visuals );
}

void platform::draw_static					( )
{
}

void platform::add_visual					( u32 id, visual_ptr v, float4x4 const& transform, bool selected, bool system_object, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &id, &v, &transform, selected, system_object, beditor );
}

void platform::update_visual				( u32 id, northstarcpp::math::float4x4 const& transform, bool selected )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &id, &transform, selected );
}

void platform::remove_visual				( u32 id )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( id );
}

void platform::update_visual_vertex_buffer	( northstarcpp::render::visual_ptr v, vectora<buffer_fragment> const& fragments )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, &fragments );
}

void platform::update_visual_index_buffer	( northstarcpp::render::visual_ptr v, vectora<buffer_fragment> const& fragments )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, &fragments );
}

void platform::change_visual_shader		( northstarcpp::render::visual_ptr v, char const* shader, char const* texture )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, shader, texture );
}

void platform::add_light					( u32 id, light_props const& props, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &id, &props, beditor );
}

void platform::update_light				( u32 id, light_props const& props, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &id, &props, beditor );
}

void platform::remove_light				( u32 id, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &id, beditor);
}

void platform::render_visuals				( )
{
}

void platform::terrain_add_cell			( visual_ptr v, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, beditor );
}

void platform::terrain_remove_cell			( visual_ptr v, bool beditor )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, beditor );
}

void platform::terrain_update_cell_buffer	( visual_ptr v, northstarcpp::vectora<buffer_fragment_NEW> const& fragments, float4x4 const& transform)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, &fragments, &transform );
}

void platform::terrain_add_cell_texture	( visual_ptr v, texture_string const & texture, u32 user_tex_id)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, &texture, user_tex_id );
}

void platform::terrain_remove_cell_texture	( visual_ptr v, u32 user_tex_id)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &v, user_tex_id );
}

void platform::terrain_exchange_texture	( texture_string const & old_texture, texture_string const & new_texture)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( &old_texture, &new_texture );
}

void platform::set_editor_render_mode			( bool beditor, bool bgame )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( beditor, bgame );
	UNREACHABLE_CODE	( );
}

} // namespace ps3
} // namespace render
} // namespace northstarcpp