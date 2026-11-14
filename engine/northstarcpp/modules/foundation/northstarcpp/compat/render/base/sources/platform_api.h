////////////////////////////////////////////////////////////////////////////
//	Created		: 02.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PLATFORM_API_H_INCLUDED
#define PLATFORM_API_H_INCLUDED

namespace northstarcpp {
namespace render {

struct platform;

	render::platform * create_render_platform( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle);

} // namespace render
} // namespace northstarcpp

#endif // #ifndef PLATFORM_API_H_INCLUDED