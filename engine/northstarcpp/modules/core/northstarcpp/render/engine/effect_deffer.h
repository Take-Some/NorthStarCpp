////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_DEFFER_FLAT_H_INCLUDED
#define BLENDER_DEFFER_FLAT_H_INCLUDED

#include <northstarcpp/render/engine/effect_deffer_base.h>

namespace northstarcpp {
namespace render_dx10 {


class effect_deffer: public effect_deffer_base
{
public:
	effect_deffer();

	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
	virtual	void load(memory::reader& mem_reader);

private:

}; // class effect_deffer_flat

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_DEFFER_FLAT_H_INCLUDED