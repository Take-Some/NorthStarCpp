////////////////////////////////////////////////////////////////////////////
//	Created		: 05.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RENDER_DEVICE_INLINE_H_INCLUDED
#define RENDER_DEVICE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace render_dx10 {

u32 device::get_width() const
{
	return m_chain_desc.BufferDesc.Width;
}

u32 device::get_height() const
{
	return m_chain_desc.BufferDesc.Height;
}

northstarcpp::math::uint2 device::get_size() const
{
	return northstarcpp::math::uint2( m_chain_desc.BufferDesc.Width, m_chain_desc.BufferDesc.Height);
}

} // namespace render
} // namespace northstarcpp



#endif // #ifndef RENDER_DEVICE_INLINE_H_INCLUDED