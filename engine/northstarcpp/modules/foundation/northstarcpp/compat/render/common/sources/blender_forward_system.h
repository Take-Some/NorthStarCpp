////////////////////////////////////////////////////////////////////////////
//	Created		: 17.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_FORWARD_SYSTEM_H_INCLUDED
#define BLENDER_FORWARD_SYSTEM_H_INCLUDED

#include "blender_deffer_base.h"

namespace northstarcpp {
namespace render{

class blender_forward_system : public blender
{
public:
	blender_forward_system();

	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);
	virtual	void load(memory::reader& mem_reader);

private:
}; // class blender_tree

} // namespace render
} // namespace northstarcpp

#endif // #ifndef BLENDER_FORWARD_SYSTEM_H_INCLUDED