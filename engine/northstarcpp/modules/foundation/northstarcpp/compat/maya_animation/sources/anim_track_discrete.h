////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIM_TRACK_DISCRETE_H_INCLUDED
#define ANIM_TRACK_DISCRETE_H_INCLUDED


namespace northstarcpp {
namespace maya_animation {

struct anim_track;
struct animation_curve_data_header;
static const float anim_fps = 30;
static const float time_delta = 1.f/anim_fps;

struct anim_track_discrete
{
		anim_track_discrete():m_start_time(0){}
		void		build			( anim_track& ad );
		void		save			( FILE* /*f*/ )const{};
		void		load			( FILE* /*f*/ ){};
static	void		setup_header	( animation_curve_data_header & ){};
		void		bone_pos		( northstarcpp::float4x4 &pose, float time );

	vector <northstarcpp::float3>			m_positions;
	vector <northstarcpp::float3>			m_scale;
	vector <northstarcpp::math::quaternion>	m_rotations;
	float							m_start_time;
}; // struct anim_track_discrete

} // namespace maya_animation
} // namespace northstarcpp 

#endif // #ifndef ANIM_TRACK_DISCRETE_H_INCLUDED