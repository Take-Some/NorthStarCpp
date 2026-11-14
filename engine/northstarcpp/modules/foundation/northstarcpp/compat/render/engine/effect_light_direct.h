////////////////////////////////////////////////////////////////////////////
//	Created		: 24.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_LIGHT_DIRECT_H_INCLUDED
#define BLENDER_LIGHT_DIRECT_H_INCLUDED

#include <northstarcpp/compat/render/core/effect_descriptor.h>

namespace northstarcpp {
namespace render_dx10 {


class effect_light_direct: public effect
{
public:
	enum
	{
		tech_sun_near,
		tech_sun_far
	};

public:
	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
}; // class effect_light_direct

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_LIGHT_DIRECT_H_INCLUDED