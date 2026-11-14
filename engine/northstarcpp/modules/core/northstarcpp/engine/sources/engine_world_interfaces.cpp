////////////////////////////////////////////////////////////////////////////
//	Created		: 04.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "engine_world.h"
#include <northstarcpp/render/base/engine_renderer.h>
#include <northstarcpp/input/world.h>
#include "rpc.h"

using northstarcpp::engine::engine_world;
using northstarcpp::math::float4x4;

void engine_world::delete_on_logic_tick						( command_type* const command )
{
	m_processed_logic.push_back			( command );
}

void engine_world::delete_on_editor_tick					( command_type* const command )
{
	m_processed_editor.push_back		( command );
}

void engine_world::exit										( int exit_code )
{
	R_ASSERT							( !m_destruction_started );

	set_exit_code						( exit_code );

	threading::interlocked_exchange		( m_destruction_started, 1 );
}

void engine_world::run_renderer_commands					( )
{
	render_world( ).engine().run		( false );
}

void engine_world::draw_frame								( )
{
	render_world( ).engine().draw_frame	( );
}

void engine_world::flush_debug_commands						( )
{
	render_world( ).engine().flush_debug_commands();
}

void engine_world::on_resize								( )
{
}

northstarcpp::render::world&	engine_world::get_renderer_world	( )
{
	return								render_world();
}

northstarcpp::sound::world&	engine_world::get_sound_world	( )
{
	return								*m_sound_world;
}

northstarcpp::animation::world&		engine_world::get_animation_world	( )
{
	return								*m_animation_world;
}

float engine_world::get_last_frame_time						( )
{
	return								m_last_frame_time;
}