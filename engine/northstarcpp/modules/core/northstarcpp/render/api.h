////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_API_H_INCLUDED
#define NORTHSTARCPP_RENDER_API_H_INCLUDED

#ifndef NORTHSTARCPP_RENDER_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_RENDER_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_RENDER_BUILDING
#			define NORTHSTARCPP_RENDER_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_RENDER_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RENDER_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RENDER_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_RENDER_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_RENDER_API

#if NORTHSTARCPP_PLATFORM_PS3
	typedef pvoid	HWND;
#endif // #if NORTHSTARCPP_PLATFORM_PS3

namespace northstarcpp {
namespace render { 

struct world;

namespace engine {
	struct wrapper;
} // namespace engine

NORTHSTARCPP_RENDER_API	world*	create_world	( render::engine::wrapper& engine, HWND window_handle );
NORTHSTARCPP_RENDER_API	void	destroy_world	( world*& world );

} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_API_H_INCLUDED