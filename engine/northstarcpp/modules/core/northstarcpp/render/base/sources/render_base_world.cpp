////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include <northstarcpp/render/base/sources/platform_api.h>
#include "command_processor.h"
#include "engine_renderer.h"
#include "editor_renderer.h"
#include "game_renderer.h"
#include "ui_renderer.h"

using northstarcpp::render::base_world;
using northstarcpp::resources::managed_resource_ptr;

base_world::base_world								( northstarcpp::render::engine::wrapper& wrapper, HWND window_handle ) :
	m_wrapper	( wrapper )
{
	m_platform	 = create_render_platform( wrapper, window_handle );
	
	ASSERT( m_platform );

	m_processor					= NEW ( command_processor )		( render::delete_on_tick_callback_type ( &wrapper, &render::engine::wrapper::delete_on_logic_tick) );
	m_editor_commands_processor = NEW ( command_processor )		( render::delete_on_tick_callback_type ( &wrapper, &render::engine::wrapper::delete_on_editor_tick) );
	m_engine	= NEW ( engine::engine_renderer )	( *this, *m_platform );
	m_game		= NEW ( game::game_renderer )		( *this, *m_platform );
	m_editor	= NEW ( editor::editor_renderer )	( *this, *m_platform );
	m_ui		= NEW ( ui_renderer )				( *this, *m_platform );
}

base_world::~base_world								( )
{
	DELETE		( m_ui );
	DELETE		( m_editor );
	DELETE		( m_game );
	DELETE		( m_engine );
	DELETE		( m_editor_commands_processor );
	DELETE		( m_processor );
	DELETE		( m_platform );
}

northstarcpp::render::engine::renderer& base_world::engine	( )
{
	ASSERT		( m_engine );
	return		( *m_engine );
}

northstarcpp::render::game::renderer&	base_world::game		( )
{
	ASSERT		( m_game );
	return		( *m_game );
}

northstarcpp::render::ui::renderer&	base_world::ui		( )
{
	ASSERT		( m_ui );
	return		( *m_ui );
}

northstarcpp::render::editor::renderer& base_world::editor	( )
{
	ASSERT		( m_editor );
	return		( *m_editor );
}


void base_world::push_command							( northstarcpp::render::engine::command* command )
{
	commands().push_command		( command );
}

void base_world::destroy_command							( northstarcpp::render::engine::command* command )
{
	R_ASSERT					( command->remove_frame_id <= m_platform->frame_id() );
	L_DELETE					( command );
}

void base_world::push_command_editor							( northstarcpp::render::engine::command* command )
{
	commands_editor().push_command		( command );
}

void base_world::destroy_command_editor							( northstarcpp::render::engine::command* command )
{
	R_ASSERT					( command->remove_frame_id <= m_platform->frame_id() );
	E_DELETE					( command );
}

void base_world::draw_frame_logic	( )
{
	m_wrapper.draw_frame_logic	( );
}

void base_world::draw_frame_editor	( )
{
	m_wrapper.draw_frame_editor	( );
}

northstarcpp::render::platform &	base_world::platform	( )
{
	return *m_platform;
}

void		base_world::clear_resources				( )
{
	m_platform->clear_resources( );
}

void		base_world::set_editor_render_mode		( bool draw_editor, bool draw_game )
{
	m_platform->set_editor_render_mode( draw_editor, draw_game );
}
