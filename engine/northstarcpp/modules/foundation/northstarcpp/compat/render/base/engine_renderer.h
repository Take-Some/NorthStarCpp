////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_ENGINE_RENDERER_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_ENGINE_RENDERER_H_INCLUDED

namespace northstarcpp {
namespace render {
namespace engine {

struct NORTHSTARCPP_NOVTABLE renderer {
	virtual	bool	run						( bool wait_for_command ) = 0;
	virtual	bool	run_editor_commands		( bool wait_for_command ) = 0;
	virtual	u32		frame_id				( ) = 0;
	virtual	void	set_view_matrix			( float4x4 const& view_matrix ) = 0;
	virtual	void	set_projection_matrix	( float4x4 const& view_matrix ) = 0;
	virtual	void	draw_frame				( ) = 0;
	virtual	void	purge_orders			( ) = 0;
	virtual	void	flush_debug_commands	( ) = 0;
	virtual	void 	test_cooperative_level	( ) = 0;

	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( renderer )
}; // class world

} // namespace engine
} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_ENGINE_RENDERER_H_INCLUDED