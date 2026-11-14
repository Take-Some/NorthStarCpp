////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "core_entry_point.h"
#include <northstarcpp/compat/os_include.h>

namespace northstarcpp {
namespace core {

static string512 s_current_directory	= "";

void platform::preinitialize( )
{
#ifdef DEBUG
	DmMapDevkitDrive	( );
#endif // #ifdef DEBUG
}

pcstr current_directory		( )
{
	if ( ! * s_current_directory )
		strings::copy	( s_current_directory, "game:\\" );

	return				s_current_directory;
}

} // namespace core
} // namespace northstarcpp