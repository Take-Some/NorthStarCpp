////////////////////////////////////////////////////////////////////////////
//	Created		: 08.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BONE_MIXER_DATA_H_INCLUDED
#define BONE_MIXER_DATA_H_INCLUDED

#include <northstarcpp/animation/anim_track_common.h>

namespace northstarcpp {
namespace animation {

struct bone_mixer_data 
{	
	bone_mixer_data	( ) : m_frame( zero )
	{
	}

	frame 	m_frame;
}; // struct bone_mixer_data

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef BONE_MIXER_DATA_H_INCLUDED