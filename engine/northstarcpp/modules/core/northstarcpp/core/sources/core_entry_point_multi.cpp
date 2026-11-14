////////////////////////////////////////////////////////////////////////////
//	Created 	: 27.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "core_entry_point.h"
#include "resources_device_manager.h"

namespace northstarcpp {
namespace core {

static string512	s_application		=	"";
static string512	s_user				=	"";

void   set_application_name ( pcstr application_name )
{
	strings::copy		(s_application, application_name);
}

pcstr   application_name ( )
{
	return				s_application;
}

pcstr   user_name ( )
{
	return				s_user;
}

pcstr   user_data_directory ( )
{
	static string512	s_user_data_directory = "";
	static bool			s_initialized = false;

	if ( !s_initialized ) {
		s_initialized	= true;
		strings::join	( s_user_data_directory, current_directory(), "user_data/" );
	}

	return				s_user_data_directory;
}

} // namespace core
} // namespace northstarcpp