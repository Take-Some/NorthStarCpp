////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_DX9_API_H_INCLUDED
#define NORTHSTARCPP_RENDER_DX9_API_H_INCLUDED

namespace northstarcpp {

namespace render {
	struct world;

	namespace engine {
		struct wrapper;
	} // namespace engine
} // namespace render

namespace render_dx9 {

	NORTHSTARCPP_RENDER_API	render::world*	create_world	( render::engine::wrapper& engine, HWND window_handle );
	NORTHSTARCPP_RENDER_API	void			destroy_world	( render::world*& world );

} // namespace render_dx9

} // namespace northstarcpp


#endif // #ifndef NORTHSTARCPP_RENDER_DX9_API_H_INCLUDED