////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RES_BUFFER_INLINE_H_INCLUDED
#define RES_BUFFER_INLINE_H_INCLUDED

#include <northstarcpp/compat/render/core/device.h>

namespace northstarcpp {
namespace render_dx10 {

void*	res_buffer::map		( D3D_MAP mode)
{
	void * res;
#if USE_DX10
	m_hw_buffer->Map( mode, 0, &res);
#else
	D3D11_MAPPED_SUBRESOURCE mapped_res;
	device::ref().d3d_context()->Map( m_hw_buffer, 0, mode, 0, &mapped_res);
	res = mapped_res.pData;
#endif

	return res;
}

void	res_buffer::unmap	()
{
#if USE_DX10
	m_hw_buffer->Unmap();
#else
	device::ref().d3d_context()->Unmap( m_hw_buffer, 0);
#endif

}

} // namespace render
} // namespace northstarcpp

#endif // #ifndef RES_BUFFER_INLINE_H_INCLUDED