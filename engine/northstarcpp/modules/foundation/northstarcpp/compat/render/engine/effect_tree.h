////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_TREE_H_INCLUDED
#define BLENDER_TREE_H_INCLUDED

#include <northstarcpp/compat/render/engine/effect_deffer_base.h>

namespace northstarcpp {
namespace render_dx10 {

class effect_tree : public effect_deffer_base
{
public:
	effect_tree();

	virtual void compile(effect_compiler& compiler, const effect_compilation_options& options);
	virtual	void load(memory::reader& mem_reader);

private:
	bool	m_blend;
	bool	m_not_an_tree;
}; // class effect_tree

} // namespace render
} // namespace northstarcpp


#endif // #ifndef BLENDER_TREE_H_INCLUDED