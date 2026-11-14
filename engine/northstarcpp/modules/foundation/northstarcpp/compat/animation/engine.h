////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ANIMATION_ENGINE_H_INCLUDED
#define NORTHSTARCPP_ANIMATION_ENGINE_H_INCLUDED

namespace northstarcpp {

namespace render {
	struct world;
} // namespace render

namespace animation {

struct NORTHSTARCPP_NOVTABLE engine {
	virtual	render::world&	get_renderer_world	( ) = 0;


protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( engine )
}; // class engine

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ANIMATION_ENGINE_H_INCLUDED