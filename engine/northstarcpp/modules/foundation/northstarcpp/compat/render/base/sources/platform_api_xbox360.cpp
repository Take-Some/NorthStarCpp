////////////////////////////////////////////////////////////////////////////
//	Created		: 03.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "platform_api.h"

namespace northstarcpp {
namespace render {

namespace xbox360 {
	render::platform * create_render_platform( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle);
} // namespace xbox360


render::platform * create_render_platform( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle)
{
	return xbox360::create_render_platform( wrapper, window_handle);
}


} // namespace render
} // namespace northstarcpp

