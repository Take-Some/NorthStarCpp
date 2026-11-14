////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED

namespace stalker2 {

class scene;
class game_world;
class game_object;
class main_menu;
class console;
class stats;
class stats_graph;
class project_cooker;
class cell_cooker;
class object_cooker;
class solid_visual_cooker;

class game :
	public northstarcpp::engine_user::world,
	private boost::noncopyable
{
public:
							game					(	northstarcpp::engine_user::engine& engine,
														northstarcpp::input::world& input,
														northstarcpp::render::world& render,
														northstarcpp::ui::world& ui,
														northstarcpp::physics::world& physics,
														northstarcpp::rtp::world& rtp,
														northstarcpp::animation::world& animation,
														northstarcpp::sound::world& sound
													);

	virtual					~game					( );

	virtual	void			enable					( bool value );
	virtual	void			tick					( u32 current_frame_id );
	virtual	void			clear_resources			( );

			void			set_view_matrix			( float4x4 const& view_matrix);
	float4x4 const&			get_inverted_view_matrix( ) const;
			void			set_inverted_view_matrix( float4x4 const& view );

			u32				time_ms					( );

	virtual	void			load					( pcstr path );
	virtual	void			unload					( pcstr );

	fs::path_string const&	project_path			( ) const;

	inline	engine_user::engine& engine				( ) const { return m_engine; }
//	game_resource_manager&	resource_manager		( ) { return *m_game_resource_manager; }
	northstarcpp::render::world&	render_world			( )	{ return m_render_world; }
	northstarcpp::input::world&		input_world				( )	{ return m_input_world; }
	northstarcpp::ui::world&		ui_world				( )	{ return m_ui_world; }
	northstarcpp::rtp::world&		rtp						( ) { return m_rtp_world; }

			void			toggle_console			( );

			void			main_menu_close_query	( );
			void			game_world_close_query	( );
			void			exit					( pcstr str );
private:
			void			test					( );
			void			switch_to_scene			( scene* scene );
			void			update_stats			( u32 const current_frame_id );
			void			register_console_commands( );

			void			register_cookers		( );
			void			unregister_cookers		( );
private:
//	game_resource_manager*					m_game_resource_manager;
	float4x4								m_inverted_view;
	float4x4								m_projection;
	stats_graph*							m_fps_graph;
	console*								m_console;
	stats*									m_stats;
	timing::timer							m_timer;
	fs::path_string							m_project_path;

	northstarcpp::engine_user::engine&				m_engine;
	northstarcpp::input::world&						m_input_world;
	northstarcpp::render::world&					m_render_world;
	northstarcpp::ui::world&						m_ui_world;
	northstarcpp::physics::world&					m_physics_world;
	northstarcpp::animation::world&					m_animation_world;
	northstarcpp::rtp::world&						m_rtp_world;
	northstarcpp::sound::world&						m_sound_world;

	game_world*								m_game_world;
	main_menu*								m_main_menu;
	scene*									m_active_scene;
	float									m_last_frame_time;
	bool									m_enabled;

	project_cooker*			m_project_cooker;
	cell_cooker*			m_cell_cooker;
	object_cooker*			m_object_cooker;
	solid_visual_cooker*	m_solid_visual_cooker;
}; // class game_world

} // namespace stalker2

#endif // #ifndef GAME_H_INCLUDED