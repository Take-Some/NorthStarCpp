////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <new>
#include "editor_world.h"

#pragma unmanaged
#include <northstarcpp/editor/world/api.h>
#pragma managed

#pragma unmanaged
static void enable_fpe( bool const value );
#pragma managed

using northstarcpp::editor::editor_world;

static bool					s_initialized = false;
static char					s_world_fake[ sizeof( editor_world ) ];
static editor_world&		s_world = ( editor_world& )s_world_fake;

northstarcpp::editor::allocator_type*	northstarcpp::editor::g_allocator = 0;

static northstarcpp::editor::world* create_world		( northstarcpp::editor::engine& engine, bool fpe_enabled )
{
#ifdef DEBUG
	enable_fpe				( fpe_enabled );
#else // #ifdef DEBUG
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( fpe_enabled );
#endif // #ifdef DEBUG

	R_ASSERT				( !s_initialized );
	s_initialized			= true;

	typedef northstarcpp::editor::editor_world	world;
	new ( &s_world ) world	( engine );
	return					( &s_world );
}

static void destroy_world						( northstarcpp::editor::world*& world )
{
	R_ASSERT				( s_initialized );
	R_ASSERT				( &s_world == world );
	s_initialized			= false;
	s_world.~editor_world	( );
	world					= 0;
}

static void install_bugtrap						( )
{
	// this is workaround on incorrect BugTrapN initialization in
	// case of call from unmanaged code: globals and statics are not initialized.
	// but when we call it from managed code first, all works properly
	IntelleSoft::BugTrap::ExceptionHandler();
}

#pragma unmanaged

#ifdef DEBUG
static void enable_fpe							( bool const value )
{
	northstarcpp::debug::enable_fpe	( value );
}
#endif // #ifdef DEBUG

northstarcpp::editor::world* northstarcpp::editor::create_world	( engine& engine )
{
#ifdef DEBUG
	bool const fpe_enabled	= northstarcpp::debug::is_fpe_enabled( );
	northstarcpp::debug::enable_fpe	( false );
#else // #ifdef DEBUG
	bool const fpe_enabled	= false;
#endif // #ifdef DEBUG
	return					::create_world( engine, fpe_enabled );
}

void northstarcpp::editor::destroy_world				( northstarcpp::editor::world*& world )
{
	::destroy_world			( world );
}

void northstarcpp::editor::memory_allocator				( allocator_type& allocator )
{
	install_bugtrap			( );

	ASSERT					( !g_allocator );
	g_allocator				= &allocator;
	allocator.user_current_thread_id();
}
#pragma managed