////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/engine/api.h>
#include "engine_world.h"
#include <northstarcpp/linkage_helper.h>

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
	DECLARE_LINKAGE_ID(engine_entry_point)
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

typedef northstarcpp::memory::doug_lea_allocator_type	doug_lea_allocator_type;
doug_lea_allocator_type							northstarcpp::engine::g_allocator;

static northstarcpp::uninitialized_reference<northstarcpp::engine::engine_world>	s_world;

void northstarcpp::engine::preinitialize				( pcstr command_line, pcstr application, pcstr build_date )
{
	NORTHSTARCPP_CONSTRUCT_REFERENCE		( s_world, engine_world )	( command_line, application, build_date );
}

void northstarcpp::engine::initialize		( northstarcpp::engine_user::module_proxy* module_proxy )
{
	s_world->setup_engine_user		( module_proxy );
	s_world->initialize				( );
}

void northstarcpp::engine::finalize						( )
{
	NORTHSTARCPP_DESTROY_REFERENCE			( s_world );
}

void northstarcpp::engine::execute						( )
{
	s_world->run					( );
}

bool northstarcpp::engine::command_line_editor			( )
{
	return							s_world->command_line_editor( );
}

bool northstarcpp::engine::command_line_no_splash_screen( )
{
	return							s_world->command_line_no_splash_screen( );
}

int northstarcpp::engine::get_exit_code					( )
{
	return s_world->get_exit_code	( );
}
