////////////////////////////////////////////////////////////////////////////
//	Created		: 24.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_COMBINE_H_INCLUDED
#define BLENDER_COMBINE_H_INCLUDED

#include <northstarcpp/compat/render/core/effect_descriptor.h>

namespace northstarcpp {
namespace render_dx10 {


class effect_combine: public effect
{
public:
	effect_combine();
	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
}; // class effect_combine

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_COMBINE_H_INCLUDED