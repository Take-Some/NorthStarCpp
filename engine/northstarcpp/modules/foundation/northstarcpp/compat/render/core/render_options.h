////////////////////////////////////////////////////////////////////////////
//	Created		: 25.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RENDER_OPTIONS_H_INCLUDED
#define RENDER_OPTIONS_H_INCLUDED

namespace northstarcpp {
namespace render_dx10 {

struct render_options 
{
	u32			smap_size;
	bool		hw_smap;
	DXGI_FORMAT	hw_smap_format;

	DXGI_FORMAT	null_rt_format;

	bool		fp16_filter;
	bool		fp16_blend;

}; // class render_options

} // namespace render
} // namespace northstarcpp

#endif // #ifndef RENDER_OPTIONS_H_INCLUDED