////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RTP_WORLD_H_INCLUDED
#define NORTHSTARCPP_RTP_WORLD_H_INCLUDED

namespace northstarcpp {

namespace render {
namespace debug {
	struct renderer;
} // namespace debug
} // namespace render

namespace input {
	struct world;
} // namespace input

namespace rtp {

struct NORTHSTARCPP_NOVTABLE world {
	virtual			~world			( ) { }
	virtual	void	render			( northstarcpp::render::debug::renderer& renderer ) const =0;
	virtual	void	tick			( ) = 0;
	virtual	void	dbg_move_control( float4x4 &view_inverted, input::world&  input ) = 0;

}; // class world

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RTP_WORLD_H_INCLUDED