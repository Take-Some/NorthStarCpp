////////////////////////////////////////////////////////////////////////////
//	Created		: 13.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/core/sampler_state_descriptor.h>
#include <northstarcpp/render/core/effect_compiler.h>


namespace northstarcpp {
namespace render_dx10 {

inline 	effect_compiler &	sampler_state_descriptor ::end_sampler ()
{
	ASSERT( m_effect_compiler);

	if( m_effect_compiler)
		return m_effect_compiler->end_sampler();
}

} // namespace render 
} // namespace northstarcpp
