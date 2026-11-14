////////////////////////////////////////////////////////////////////////////
//	Created		: 20.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_DEFFER_MATERIALS_H_INCLUDED
#define BLENDER_DEFFER_MATERIALS_H_INCLUDED

#include <northstarcpp/render/engine/effect_deffer_base.h>


namespace northstarcpp {
namespace render_dx10 {


class effect_deffer_materials: public effect_deffer_base
{

public:
	effect_deffer_materials( u32 material_index);

	virtual void compile( effect_compiler& compiler, const effect_compilation_options& options);
	virtual	void load	( memory::reader& mem_reader);

private:
	u32 m_material_ind;

}; // class effect_deffer_materials

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_DEFFER_MATERIALS_H_INCLUDED