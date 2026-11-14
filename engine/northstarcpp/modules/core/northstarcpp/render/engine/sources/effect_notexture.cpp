////////////////////////////////////////////////////////////////////////////
//	Created		: 24.04.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/engine/effect_notexture.h>
#include <northstarcpp/render/core/effect_compiler.h>

namespace northstarcpp {
namespace render_dx10 {

void effect_notexture::compile(effect_compiler& compiler, const effect_compilation_options& options)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( options );

	shader_defines_list	defines;
	make_defines(defines);

 	compiler.begin_technique(/*SE_R2_SHADOW*/)
 		.begin_pass	("test", "color_fixed", defines)
			.set_fill_mode	( D3D_FILL_WIREFRAME)
 		.end_pass	()
 	.end_technique();
}

} // namespace render
} // namespace northstarcpp
