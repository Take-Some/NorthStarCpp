////////////////////////////////////////////////////////////////////////////
//	Created		: 25.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_LIGHT_MASK_H_INCLUDED
#define BLENDER_LIGHT_MASK_H_INCLUDED

namespace northstarcpp {
namespace render{

class blender_light_mask: public blender
{
public:
	enum
	{
		tech_mask_spot,
		tech_mask_point,
		tech_mask_direct,
		tech_mask_stencil
	};

public:
	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);
}; // class blender_light_mask

} // namespace northstarcpp
} // namespace render

#endif // #ifndef BLENDER_LIGHT_MASK_H_INCLUDED