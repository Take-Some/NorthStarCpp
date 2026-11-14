////////////////////////////////////////////////////////////////////////////
//	Created		: 24.04.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BLENDER_NOTEXTURE_H_INCLUDED
#define BLENDER_NOTEXTURE_H_INCLUDED

#include "blender_deffer_base.h"

namespace northstarcpp {
namespace render {


class blender_notexture: public blender
{
public:
	blender_notexture():blender(){}
	~blender_notexture(){}

	virtual void compile(blender_compiler& compiler, const blender_compilation_options& options);

private:

}; // class blender_notexture

} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef BLENDER_NOTEXTURE_H_INCLUDED