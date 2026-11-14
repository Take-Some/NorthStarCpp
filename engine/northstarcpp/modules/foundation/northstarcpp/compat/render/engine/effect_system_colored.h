////////////////////////////////////////////////////////////////////////////
//	Created		: 17.05.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_SYSTEM_COLORED_H_INCLUDED
#define BLENDER_SYSTEM_COLORED_H_INCLUDED

#include "northstarcpp/compat/render/core/effect_descriptor.h"

namespace northstarcpp {
namespace render_dx10 {

class effect_system_colored: public effect
{
public:
	effect_system_colored () : effect() {}
	~effect_system_colored() {}

	enum techniques{ solid, stenciled, voided };

	virtual void compile( effect_compiler& compiler, const effect_compilation_options& options);

private:

};

} // namespace render_dx10
} // namespace northstarcpp

#endif // #ifndef BLENDER_SYSTEM_COLORED_H_INCLUDED