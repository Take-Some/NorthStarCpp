////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ENGINE_API_H_INCLUDED
#define NORTHSTARCPP_ENGINE_API_H_INCLUDED

#ifndef NORTHSTARCPP_ENGINE_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_ENGINE_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_ENGINE_LIBRARY_BUILDING
#			define NORTHSTARCPP_ENGINE_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_ENGINE_LIBRARY_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_ENGINE_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_ENGINE_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_ENGINE_LIBRARY_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_ENGINE_API

namespace northstarcpp {

namespace engine_user {
	struct module_proxy;
} // namespace engine_user

namespace engine {

NORTHSTARCPP_ENGINE_API	void	preinitialize					( pcstr command_line, pcstr application, pcstr build_date );
NORTHSTARCPP_ENGINE_API	void	initialize						( northstarcpp::engine_user::module_proxy* module_proxy );
NORTHSTARCPP_ENGINE_API	void	execute							( );
NORTHSTARCPP_ENGINE_API	int		get_exit_code					( );
NORTHSTARCPP_ENGINE_API	void	finalize						( );
NORTHSTARCPP_ENGINE_API	bool	command_line_editor				( );
NORTHSTARCPP_ENGINE_API	bool	command_line_no_splash_screen	( );

} // namespace engine
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ENGINE_API_H_INCLUDED