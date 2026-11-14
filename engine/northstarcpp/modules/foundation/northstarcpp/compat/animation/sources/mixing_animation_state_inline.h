////////////////////////////////////////////////////////////////////////////
//	Created		: 21.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXING_ANIMATION_STATE_INLINE_H_INCLUDED
#define MIXING_ANIMATION_STATE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline animation_state::animation_state	(
		animation_clip const& animation,
		animation::bone_mixer_data& bone_mixer_data,
		float const time,
		float const weight
	) :
	animation		( &animation ),
	bone_mixer_data	( &bone_mixer_data ),
	time			( time ),
	weight			( weight )
{
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MIXING_ANIMATION_STATE_INLINE_H_INCLUDED