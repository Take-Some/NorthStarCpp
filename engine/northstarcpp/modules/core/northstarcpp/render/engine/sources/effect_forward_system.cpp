////////////////////////////////////////////////////////////////////////////
//	Created		: 17.02.2010
//	Author		: Armen Abroyan
//	Copyright ( C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/engine/effect_forward_system.h>
#include <northstarcpp/render/core/effect_compiler.h>

namespace northstarcpp {
namespace render_dx10 {

effect_forward_system::effect_forward_system() 
{
}

void effect_forward_system::compile( effect_compiler& compiler, const effect_compilation_options& options)
{
		NORTHSTARCPP_UNREFERENCED_PARAMETER	( options);

		shader_defines_list	defines;
		make_defines( defines);

		compiler.begin_technique( /*LEVEL*/);
		compiler.begin_pass			( "test", "test", defines)
				.set_depth			( true, true)
				.set_alpha_blend	( false)
				.set_texture		( "t_base", options.tex_list[0])
			.end_pass		()
		.end_technique	();

		return;
}

void effect_forward_system::load( memory::reader& mem_reader)
{
	effect::load( mem_reader);
}

} // namespace render
} // namespace northstarcpp
