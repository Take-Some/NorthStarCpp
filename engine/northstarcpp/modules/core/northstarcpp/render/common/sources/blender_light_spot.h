////////////////////////////////////////////////////////////////////////////
//	Created		: 22.04.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_LIGHT_SPOT_H_INCLUDED
#define BLENDER_LIGHT_SPOT_H_INCLUDED

namespace northstarcpp {
namespace render{

class blender_light_spot: public blender
{
public:
	enum
	{
		//tech_fill,
		tech_unshadowed
	};
private:
	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);

}; // class blender_light_spot

} // namespace render
} // namespace northstarcpp


#endif // #ifndef BLENDER_LIGHT_SPOT_H_INCLUDED