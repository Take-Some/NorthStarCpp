////////////////////////////////////////////////////////////////////////////
//	Created		: 25.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_LIGHT_MASK_H_INCLUDED
#define BLENDER_LIGHT_MASK_H_INCLUDED

#include <northstarcpp/compat/render/core/effect_descriptor.h>

namespace northstarcpp {
namespace render_dx10 {

class effect_light_mask: public effect
{
public:
	enum
	{
		tech_mask_spot,
		tech_mask_point,
		tech_mask_direct,
		tech_mask_stencil
	};

public:
	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
}; // class effect_light_mask

} // namespace northstarcpp
} // namespace render

#endif // #ifndef BLENDER_LIGHT_MASK_H_INCLUDED