////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "network_world.h"

#if !NORTHSTARCPP_PLATFORM_PS3
#	include "io_service.h"
#endif // #if !NORTHSTARCPP_PLATFORM_PS3

#ifndef MASTER_GOLD
static northstarcpp::command_line::key	s_no_network_key	("no_network", "", "", "disable network");
#endif // #ifndef MASTER_GOLD

using northstarcpp::network::network_world;

network_world::network_world		( northstarcpp::network::engine& engine ) :
	m_engine					( engine )
{
#ifndef MASTER_GOLD
	if ( !s_no_network_key.is_set() )
#endif // #ifndef MASTER_GOLD
#if !NORTHSTARCPP_PLATFORM_PS3
		m_ioservice				= XN_NEW(lowlevel::io_service);	
#else // #if !NORTHSTARCPP_PLATFORM_PS3
		(void)0;
#endif // #if !NORTHSTARCPP_PLATFORM_PS3
}

network_world::~network_world		( )
{
#if !NORTHSTARCPP_PLATFORM_PS3
	XN_DELETE					( m_ioservice );
#endif // #if !NORTHSTARCPP_PLATFORM_PS3
}

void network_world::tick			( u32 const logic_frame_id )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( logic_frame_id );
#ifndef MASTER_GOLD
	if ( !s_no_network_key.is_set() )
#endif // #ifndef MASTER_GOLD
#if !NORTHSTARCPP_PLATFORM_PS3
		m_ioservice->tick		( );
#else // #if !NORTHSTARCPP_PLATFORM_PS3
		(void)0;
#endif // #if !NORTHSTARCPP_PLATFORM_PS3
}

void network_world::clear_resources	( )
{
}