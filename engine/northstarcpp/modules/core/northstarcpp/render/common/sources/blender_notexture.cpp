////////////////////////////////////////////////////////////////////////////
//	Created		: 24.04.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "blender_notexture.h"

namespace northstarcpp {
namespace render{

void blender_notexture::compile(blender_compiler& compiler, const blender_compilation_options& options)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( options );

	shader_defines_list	defines;
	make_defines(defines);

 	compiler.begin_technique(/*SE_R2_SHADOW*/)
 		.begin_pass("test", "color", defines)
 		.end_pass()
 	.end_technique();
 }

} // namespace render
} // namespace northstarcpp
