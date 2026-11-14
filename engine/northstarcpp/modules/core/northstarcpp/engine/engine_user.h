////////////////////////////////////////////////////////////////////////////
//	Created		: 19.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ENGINE_ENGINE_USER_H_INCLUDED
#define NORTHSTARCPP_ENGINE_ENGINE_USER_H_INCLUDED

namespace northstarcpp {

namespace editor {
	struct world;
} // namespace editor

namespace animation {
	struct world;
} // namespace animation

namespace input {
	struct world;
} // namespace input

namespace render {
	struct world;

	namespace engine {
		struct command;
	} // namespace engine
} // namespace render

namespace network {
	struct engine;
	struct world;
} // namespace game

namespace sound {
	struct engine;
	struct world;
} // namespace game

namespace ui {
	struct world;
} // namespace ui

namespace physics {
	struct world;
} // namespace physics

namespace rtp {
	struct world;
} // namespace physics


namespace engine_user {

typedef memory::doug_lea_allocator_type	allocator_type;

struct NORTHSTARCPP_NOVTABLE engine {
	virtual	float	get_last_frame_time	( ) = 0;
	virtual	void	exit				( int exit_code ) = 0;
	virtual	void	enter_editor_mode	( ) = 0;
	virtual	bool	command_line_editor	( ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( engine )
}; // class engine

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void	enable				( bool value ) = 0;
	virtual	void	tick				( u32 current_frame_id ) = 0;
	virtual	void	clear_resources		( ) = 0;
	virtual	void 	load				( pcstr project_name ) = 0;
	virtual	void 	unload				( pcstr ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

struct NORTHSTARCPP_NOVTABLE module_proxy {
	virtual	world*	create_world		(
						northstarcpp::engine_user::engine& engine, 
						northstarcpp::input::world& input, 
						northstarcpp::render::world& render, 
						northstarcpp::ui::world& ui,
						northstarcpp::physics::world& physics,
						northstarcpp::rtp::world& rtp,
						northstarcpp::animation::world& animation,
						northstarcpp::sound::world& sound
					) = 0;
	virtual	void	destroy_world		( world*& world ) = 0;
	virtual	void	set_memory_allocator( allocator_type& allocator ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( module_proxy )
}; // class module_proxy

} // namespace engine_user
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ENGINE_ENGINE_USER_H_INCLUDED