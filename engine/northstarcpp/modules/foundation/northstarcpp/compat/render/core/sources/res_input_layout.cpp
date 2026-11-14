////////////////////////////////////////////////////////////////////////////
//	Created		: 15.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/core/device.h>
#include <northstarcpp/compat/render/core/res_input_layout.h>
#include <northstarcpp/compat/render/core/resource_manager.h>

namespace northstarcpp {
namespace render_dx10 {

res_input_layout::res_input_layout( res_declaration const * decl, res_signature const * signature):
m_declaration	( decl),
m_signature		( signature)
{
	m_hw_input_layout = NULL;
	ID3DBlob * hw_singiture = signature->hw_signiture();
	device::ref().d3d_device()->CreateInputLayout( &m_declaration->dcl_code[0], m_declaration->dcl_code.size(), hw_singiture ->GetBufferPointer(), hw_singiture->GetBufferSize(), &m_hw_input_layout);
	ASSERT( m_hw_input_layout);
}

res_input_layout::~res_input_layout	()
{
	safe_release( m_hw_input_layout);
}

void res_input_layout::_free() const
{
	resource_manager::ref().release( const_cast<res_input_layout*>(this) );
}


} // namespace render
} // namespace northstarcpp
