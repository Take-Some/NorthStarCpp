////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BUILD_EXTENSIONS_H_INCLUDED
#define BUILD_EXTENSIONS_H_INCLUDED

namespace northstarcpp {
namespace build {
					void	preinitialize				( pcstr const build_date );
					void	initialize					( core::engine * );
	NORTHSTARCPP_CORE_API	u32		calculate_build_id			( pcstr current_date );
					bool	print_build_id_command_line ( );
	NORTHSTARCPP_CORE_API	pcstr	build_date					( );
} // namespace build
} // namespace northstarcpp

#endif // #ifndef BUILD_EXTENSIONS_H_INCLUDED