////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BONE_ANIMATION_H_INCLUDED
#define BONE_ANIMATION_H_INCLUDED

#include "poly_curve.h"
#include <northstarcpp/compat/animation/anim_track_common.h>

namespace northstarcpp {
namespace animation {

typedef poly_curve< poly_curve_order3_domain<float,1> > animation_curve_type;

class bone_animation {

public:
	const	animation_curve_type	&channel	( enum_channel_id id )const		{ return m_channels[id]; }
			animation_curve_type	&channel	( enum_channel_id id )			{ return m_channels[id]; }

public:
inline		float					max_time	( )const { return m_channels[channel_translation_x].max_param(); }
inline		float					min_time	( )const { return m_channels[channel_translation_x].min_param(); }
inline		void					get_frame	( float time, frame &f )const;
			void					bone_matrix	( float time, const float3 &parent_scale, float4x4 &matrix, float3 &scale  )const;

private:
	animation_curve_type	m_channels[channel_max];

}; // class bone_animation

} // namespace animation
} // namespace northstarcpp

#include "bone_animation_inline.h"

#endif // #ifndef BONE_ANIMATION_H_INCLUDED