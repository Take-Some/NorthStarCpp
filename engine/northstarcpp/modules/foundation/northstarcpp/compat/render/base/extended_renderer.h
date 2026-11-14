////////////////////////////////////////////////////////////////////////////
//	Created 	: 18.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_EXTENDED_RENDERER_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_EXTENDED_RENDERER_H_INCLUDED

namespace northstarcpp {
namespace render {
namespace extended {

struct NORTHSTARCPP_NOVTABLE renderer {
	virtual	pcstr	type		( ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( renderer )
}; // class world

} // namespace extended
} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_EXTENDED_RENDERER_H_INCLUDED