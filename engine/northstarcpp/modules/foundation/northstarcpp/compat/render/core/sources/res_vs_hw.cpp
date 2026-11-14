////////////////////////////////////////////////////////////////////////////
//	Created		: 06.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/core/device.h>
#include <northstarcpp/compat/render/core/res_vs_hw.h>
#include <northstarcpp/compat/render/core/res_xs_hw_impl.h>

namespace northstarcpp {
namespace render_dx10 {


template<>
HRESULT res_xs_hw<vs_data>::create_hw_shader( ID3DBlob* shader_code, vs_data::hw_interface **hw_shader)
{
	void const* bytecode_buffer	= shader_code->GetBufferPointer();
	u32 bytecode_buffer_size	= (u32)shader_code->GetBufferSize();

	m_shader_data.signature = resource_manager::ref().create_signature( shader_code);

#if USE_DX10
	return device::ref().d3d_device()->CreateVertexShader	( bytecode_buffer, bytecode_buffer_size, hw_shader);
#else
	return device::ref().d3d_device()->CreateVertexShader	( bytecode_buffer, bytecode_buffer_size, NULL, hw_shader);
#endif
}

// template<>
// void res_xs_hw<vs_data>::apply()
// {
// 
// }

template class res_xs_hw<vs_data>;

} // namespace render
} // namespace northstarcpp
