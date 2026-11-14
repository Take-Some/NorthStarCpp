////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RTP_ENGINE_H_INCLUDED
#define NORTHSTARCPP_RTP_ENGINE_H_INCLUDED

namespace northstarcpp {

namespace animation {
	struct world;
} // namespace animation 

namespace render {
struct world;
} // namespace render

namespace rtp {

struct NORTHSTARCPP_NOVTABLE engine {
	virtual						~engine					( ) { }
	virtual	animation::world&	get_animation_world		( ) = 0;
	virtual	render::world&		get_renderer_world		( ) = 0;
	virtual	pcstr				get_resource_path		( ) const=0;
}; // class engine

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RTP_ENGINE_H_INCLUDED