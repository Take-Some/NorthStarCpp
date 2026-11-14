////////////////////////////////////////////////////////////////////////////
//	Created		: 21.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INPUT_LAYOUT_UTILS_H_INCLUDED
#define INPUT_LAYOUT_UTILS_H_INCLUDED

#include <d3d9types.h>

namespace northstarcpp {
namespace render_dx10 {
namespace decl_utils {

void	convert_vertex_declaration( const vector<D3DVERTEXELEMENT9> &declIn, vector<D3D_INPUT_ELEMENT_DESC> &declOut);

} // namespace decl_utils
} // namespace render
} // namespace northstarcpp

#endif // #ifndef INPUT_LAYOUT_UTILS_H_INCLUDED