////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "debug.h"

namespace northstarcpp {
namespace core {
namespace debug {

northstarcpp::command_line::key		g_fpe_disabled("fpe_disabled", "", "math", "turns off fpu exceptions");

struct debug_preinitializer
{
	debug_preinitializer	( )
	{
		preinitialize		( );
	}
};

static debug_preinitializer	s_preinitializer;

} // namespace debug
} // namespace core
} // namespace northstarcpp