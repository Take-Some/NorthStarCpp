////////////////////////////////////////////////////////////////////////////
//	Created		: 24.04.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_NOTEXTURE_H_INCLUDED
#define BLENDER_NOTEXTURE_H_INCLUDED

#include <northstarcpp/compat/render/engine/effect_deffer_base.h>

namespace northstarcpp {
namespace render_dx10 {


class effect_notexture: public effect
{
public:
	effect_notexture():effect(){}
	~effect_notexture(){}

	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);

private:

}; // class effect_notexture

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_NOTEXTURE_H_INCLUDED