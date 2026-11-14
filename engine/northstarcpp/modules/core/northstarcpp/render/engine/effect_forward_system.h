////////////////////////////////////////////////////////////////////////////
//	Created		: 17.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_FORWARD_SYSTEM_H_INCLUDED
#define BLENDER_FORWARD_SYSTEM_H_INCLUDED

#include <northstarcpp/render/engine/effect_deffer_base.h>

namespace northstarcpp {
namespace render_dx10 {

class effect_forward_system : public effect
{
public:
	effect_forward_system();

	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
	virtual	void load(memory::reader& mem_reader);

private:
}; // class effect_tree

} // namespace render
} // namespace northstarcpp

#endif // #ifndef BLENDER_FORWARD_SYSTEM_H_INCLUDED