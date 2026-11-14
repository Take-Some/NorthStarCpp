////////////////////////////////////////////////////////////////////////////
//	Created		: 15.04.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef STAGE_COMBINE_H_INCLUDED
#define STAGE_COMBINE_H_INCLUDED

#include "stage.h"
namespace northstarcpp {
namespace render{


class stage_combine: public stage
{
public:
	stage_combine(scene_context* context);
	~stage_combine();

	void execute( );

private:
	ref_shader		m_sh_combine;
	ref_geometry	m_g_combine;
}; // class stage_combine

	} // namespace render
} // namespace northstarcpp


#endif // #ifndef STAGE_COMBINE_H_INCLUDED