////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_NETWORK_API_H_INCLUDED
#define NORTHSTARCPP_NETWORK_API_H_INCLUDED

#ifndef NORTHSTARCPP_NETWORK_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_NETWORK_API					extern
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_NETWORK_BUILDING
#			define NORTHSTARCPP_NETWORK_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_NETWORK_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_NETWORK_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_NETWORK_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_NETWORK_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_NETWORK_API

namespace northstarcpp {
namespace network {

struct engine;
struct world;

typedef memory::doug_lea_allocator_type			allocator_type;

NORTHSTARCPP_NETWORK_API	world*	create_world		( engine& engine );
NORTHSTARCPP_NETWORK_API	void	destroy_world		( world*& world );
NORTHSTARCPP_NETWORK_API	void	memory_allocator	( allocator_type& allocator );
NORTHSTARCPP_NETWORK_API	void	dispatch_callbacks	( );

} // namespace network
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_NETWORK_API_H_INCLUDED