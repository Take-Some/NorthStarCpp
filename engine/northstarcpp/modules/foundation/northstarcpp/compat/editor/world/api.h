////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_EDITOR_API_H_INCLUDED
#define NORTHSTARCPP_EDITOR_API_H_INCLUDED

#include <northstarcpp/compat/memory_pthreads3_allocator.h>
#include <northstarcpp/compat/memory_allocator_helper.h>

#ifndef NORTHSTARCPP_EDITOR_API

#	ifndef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_EDITOR_BUILDING
#			define NORTHSTARCPP_EDITOR_API	NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_EDITOR_BUILDING
#			define NORTHSTARCPP_EDITOR_API	NORTHSTARCPP_DLL_IMPORT
#		endif // #ifdef NORTHSTARCPP_EDITOR_BUILDING
#	else // #ifndef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_EDITOR_API
#	endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#	define NORTHSTARCPP_EDITOR_API_C		extern "C" NORTHSTARCPP_EDITOR_API
#endif // #ifndef NORTHSTARCPP_EDITOR_API

namespace northstarcpp {
namespace editor {

	struct engine;
	struct world;

	typedef memory::doug_lea_allocator			allocator_type;

	typedef world* ( *create_world_ptr )		( engine& engine );
	NORTHSTARCPP_EDITOR_API_C	world*	create_world	( engine& engine );

	typedef void ( *destroy_world_ptr )			( world*& world );
	NORTHSTARCPP_EDITOR_API_C	void	destroy_world	( world*& world );

	typedef void ( *memory_allocator_ptr )		( allocator_type& allocator );
	NORTHSTARCPP_EDITOR_API_C	void	memory_allocator( allocator_type& allocator );

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_EDITOR_API_H_INCLUDED