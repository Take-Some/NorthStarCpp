////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/os_include.h>

namespace northstarcpp {
namespace core {

pcstr   get_command_line ( )
{
	static string512	s_command_line	=	"";
	static bool			s_initialized	=	false;

	if ( ! s_initialized )
	{
		strings::copy						(s_command_line, GetCommandLine());
		s_initialized					=	true;
	}

	return									s_command_line;
}

} // namespace core
} // namespace northstarcpp

