////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INPUT_API_H_INCLUDED
#define NORTHSTARCPP_INPUT_API_H_INCLUDED

#ifndef NORTHSTARCPP_INPUT_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_INPUT_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_INPUT_BUILDING
#			define NORTHSTARCPP_INPUT_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_INPUT_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_INPUT_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_INPUT_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_INPUT_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_INPUT_API

#ifndef _MSC_VER
	typedef pvoid HWND;
#endif // #ifndef _MSC_VER

namespace northstarcpp {
namespace input {

struct engine;
struct world;

typedef memory::doug_lea_allocator_type	allocator_type;

NORTHSTARCPP_INPUT_API	world*	create_world	( engine& engine, HWND window_handle );
NORTHSTARCPP_INPUT_API	void	destroy_world	( world*& world );
NORTHSTARCPP_INPUT_API	void	memory_allocator( allocator_type& allocator );

} // namespace input
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_INPUT_API_H_INCLUDED