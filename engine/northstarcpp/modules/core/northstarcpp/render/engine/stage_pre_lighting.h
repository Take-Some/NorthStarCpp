////////////////////////////////////////////////////////////////////////////
//	Created		: 04.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef STAGE_PRE_LIGHTING_H_INCLUDED
#define STAGE_PRE_LIGHTING_H_INCLUDED

#include "stage.h"

namespace northstarcpp {
namespace render_dx10 {

class stage_pre_lighting: public stage
{
public:
	stage_pre_lighting	( scene_context* context): stage( context){}
	~stage_pre_lighting	( ){}

	void execute( );

}; // class stage_pre_lighting

} // namespace render
} // namespace northstarcpp

#endif // #ifndef STAGE_PRE_LIGHTING_H_INCLUDED