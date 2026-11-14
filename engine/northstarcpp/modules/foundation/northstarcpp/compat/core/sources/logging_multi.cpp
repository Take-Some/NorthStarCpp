////////////////////////////////////////////////////////////////////////////
//	Created		: 27.10.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "logging.h"

void northstarcpp::logging::write_to_stdout				( pcstr format, ... )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( format );
}

bool northstarcpp::logging::initialize_console			( )
{
	return						false;
}

void northstarcpp::logging::finalize_console			( )
{
}

void northstarcpp::logging::initialize_stdout_if_needed ( )
{
}