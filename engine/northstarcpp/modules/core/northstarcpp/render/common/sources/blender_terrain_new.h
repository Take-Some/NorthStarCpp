////////////////////////////////////////////////////////////////////////////
//	Created		: 27.01.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_TERRAIN_NEW_H_INCLUDED
#define BLENDER_TERRAIN_NEW_H_INCLUDED

#include "blender_deffer_base.h"

namespace northstarcpp {
namespace render{

class blender_terrain_NEW : public blender_deffer_base
{
public:
	blender_terrain_NEW();

	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);
	virtual	void load(memory::reader& mem_reader);

private:
	bool	m_blend;
	bool	m_not_an_tree;
}; // class blender_terrain_NEW

} // namespace render
} // namespace northstarcpp


#endif // #ifndef BLENDER_TERRAIN_NEW_H_INCLUDED