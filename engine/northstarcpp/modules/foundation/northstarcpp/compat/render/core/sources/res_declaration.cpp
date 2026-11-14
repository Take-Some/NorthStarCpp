////////////////////////////////////////////////////////////////////////////
//	Created		: 07.02.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/core/resource_manager.h>

namespace northstarcpp {
namespace render_dx10 {

signiture_layout_pair::signiture_layout_pair( res_declaration const* decl, res_signature const * signature):
signature	(signature)
{
	input_layout = resource_manager::ref().create_input_layout( decl, signature);
}

res_declaration::res_declaration( D3D_INPUT_ELEMENT_DESC const* decl, u32 count):
dcl_code( decl, decl+count)
{
// 	R_CHK(device::ref().device()->CreateVertexDeclaration(decl, &m_dcl));
// 	u32 dcl_size = D3DXGetDeclLength(decl)+1;
//	m_dcl_code.assign();
}

void res_declaration::_free() const
{
	resource_manager::ref().release( const_cast<res_declaration*>(this) );
}

res_declaration::~res_declaration()
{	
	//	Release vertex layout
//	safe_release(m_dcl);
}

} // namespace render 
} // namespace northstarcpp 
