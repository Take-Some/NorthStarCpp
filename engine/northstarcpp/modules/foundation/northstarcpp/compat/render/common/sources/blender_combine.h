////////////////////////////////////////////////////////////////////////////
//	Created		: 24.03.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_COMBINE_H_INCLUDED
#define BLENDER_COMBINE_H_INCLUDED

namespace northstarcpp {
namespace render {


class blender_combine: public blender
{
public:
	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);
}; // class blender_combine

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_COMBINE_H_INCLUDED