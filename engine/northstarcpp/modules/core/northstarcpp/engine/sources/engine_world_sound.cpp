////////////////////////////////////////////////////////////////////////////
//	Created		: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "engine_world.h"
#include "rpc.h"
#include <northstarcpp/sound/world.h>
#include <northstarcpp/sound/api.h>
#include <northstarcpp/render/base/engine_renderer.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS
#	include <northstarcpp/os_preinclude.h>
#	undef NOUSER
#	undef NOMSG
#	undef NOMB
#	include <northstarcpp/os_include.h>
#	include <objbase.h>						// for COINIT_MULTITHREADED
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS

using northstarcpp::engine::engine_world;

void engine_world::initialize_sound_modules	( )
{
	rpc::assign_thread_id			( rpc::sound, threading::current_thread_id( ) );
	sound::memory_allocator			( m_sound_allocator );
	m_sound_allocator.user_current_thread_id();

	m_engine_user_allocator.user_current_thread_id();
	m_editor_allocator.user_current_thread_id();
	m_sound_world					= sound::create_world( *this, &m_engine_user_allocator, command_line_editor() ? &m_editor_allocator : 0 );
}

void engine_world::initialize_sound	( )
{
	if ( threading::g_debug_single_thread ) {
		initialize_sound_modules	( );
		return;
	}

	rpc::assign_thread_id			( rpc::sound, u32(-1) );
	threading::spawn				( boost::bind( &engine_world::sound, this ), 
									  "sound", 
									  "sound", 
									  0,
									  5 % threading::core_count() );
}

static void sound_dispatch_callbacks		( )
{
	northstarcpp::resources::dispatch_callbacks	( );
//	northstarcpp::logic::dispatch_callbacks		( );
}

void engine_world::sound_tick			( )
{
	sound_dispatch_callbacks		( );
	m_sound_world->tick				( m_logic_frame_id );
}

void engine_world::sound			( )
{
#if NORTHSTARCPP_PLATFORM_WINDOWS
	CoInitializeEx			( 0, COINIT_APARTMENTTHREADED );
//	CoInitializeEx			( 0, COINIT_MULTITHREADED );
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS

	initialize_sound_modules		( );
	rpc::process					( rpc::sound );

	for (;;) {
		if ( m_destruction_started )
			break;

#if 0
		sound_tick					( );
#else
		threading::yield			( 10 );
#endif
		while ( ( m_logic_frame_id > m_render_world->engine().frame_id( ) + 1 ) && !m_destruction_started ) {
			if ( !rpc::try_process_single_call( rpc::sound ) )
				threading::yield	( 1 );
		}

	}
	
	rpc::process					( rpc::sound );
}

void engine_world::sound_clear_resources	( )
{
	resources::dispatch_callbacks	( );
	m_sound_world->clear_resources	( );
}
