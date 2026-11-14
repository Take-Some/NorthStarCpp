////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MAYA_ANIMATION_DATA_H_INCLUDED
#define MAYA_ANIMATION_DATA_H_INCLUDED

#include <northstarcpp/compat/maya_animation/anim_track.h>

namespace northstarcpp {
namespace maya_animation {

class animation_data {

public:
			bool		load				( const char* fn );
			void		clear				( );
			void		bone_pose			( northstarcpp::float4x4 &pose, u32 id, float time );
			u32			anim_bone_count		( ){ return m_bone_animations.size(); }
			float		min_time			();
			float		max_time			();
			float		evaluate_mean_time	();
	inline	anim_track	&bone_animation		( u32 id ) { return m_bone_animations[id]; }

private:
	vector <anim_track>			m_bone_animations;

}; // class animation_data

} // namespace maya_animation
} // namespace northstarcpp

#endif // #ifndef MAYA_ANIMATION_DATA_H_INCLUDED