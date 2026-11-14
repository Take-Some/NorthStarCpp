////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "command_delegate.h"

using northstarcpp::render::command_delegate;

command_delegate::command_delegate	( Delegate const& delegate ) :
	m_delegate	( delegate )
{
	ASSERT		( delegate );
}

void command_delegate::execute		( )
{
	m_delegate	( );
}