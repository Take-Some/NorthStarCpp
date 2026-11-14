////////////////////////////////////////////////////////////////////////////
//	Created		: 08.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_ANIMATION_PLAYER_H_INCLUDED
#define TEST_ANIMATION_PLAYER_H_INCLUDED

namespace northstarcpp {

namespace render {
namespace debug {
	struct renderer;
} // namespace debug
} // namespace render

namespace animation {

struct test_mixing_animation_group;
class  skeleton;
class  mixer;
class  bone_mixer;

namespace mixing {
	class animation_clip;
} // namespace mixing

class test_animation_player {
public:
					test_animation_player	( );
					~test_animation_player	( );
			void	tick					( );
			void	render					( render::debug::renderer	&renderer );

private:
			void	on_test_config_loaded	( northstarcpp::resources::queries_result& resource );
			void	check_loaded			(  );
			//void	add_animation			( configs::lua_config_value const &cfg, northstarcpp::resources::query_result_for_user const &anim );
private:
	typedef test_mixing_animation_group*			animation_group;
	typedef deque< animation_group >				animation_groups_type;
	typedef vector< animation_group >				animation_groups_backup_type;

private:
	timing::timer						m_timer;
	animation_groups_type				m_animation_groups;
	animation_groups_backup_type		m_animation_groups_backup;
	configs::lua_config_ptr				m_config;
	resources::unmanaged_resource_ptr	m_skeleton_resource;
	skeleton const*						m_skeleton;
	bone_mixer*							m_bone_mixer;
	mixer*								m_mixer;
	float								m_animation_start_time;
	bool								m_first_tick;
	bool								m_loaded;
}; // class test_animation_player

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef TEST_ANIMATION_PLAYER_H_INCLUDED