////////////////////////////////////////////////////////////////////////////
//	Created 	: 27.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CORE_INTERNAL_H_INCLUDED
#define CORE_INTERNAL_H_INCLUDED

namespace northstarcpp {
namespace core {
namespace platform {

void	preinitialize			( );

} // namespace platform

void	fill_user_name			( );

pcstr	user_name				( );
pcstr	current_directory		( );
pcstr	application_name		( );
pcstr   get_command_line		( );
pcstr	user_data_directory		( );

void	set_application_name	( pcstr application_name );

} // namespace core
} // namespace northstarcpp

#endif // #ifndef CORE_INTERNAL_H_INCLUDED