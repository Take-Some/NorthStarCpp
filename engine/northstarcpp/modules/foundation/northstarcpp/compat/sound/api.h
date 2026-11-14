////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_SOUND_API_H_INCLUDED
#define NORTHSTARCPP_SOUND_API_H_INCLUDED

#ifndef NORTHSTARCPP_SOUND_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_SOUND_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_SOUND_BUILDING
#			define NORTHSTARCPP_SOUND_API					NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_SOUND_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_SOUND_API				NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_SOUND_API				NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_SOUND_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_SOUND_API

namespace northstarcpp {
namespace sound {

struct engine;
struct world;

typedef ::northstarcpp::memory::base_allocator		base_allocator_type;
typedef memory::doug_lea_allocator_type		allocator_type;

NORTHSTARCPP_SOUND_API	world*	create_world		( engine& engine, base_allocator_type* logic_allocator, base_allocator_type* editor_allocator );
NORTHSTARCPP_SOUND_API	void	destroy_world		( world*& world );
NORTHSTARCPP_SOUND_API	void	memory_allocator	( allocator_type& allocator );
NORTHSTARCPP_SOUND_API	void	dispatch_logic_callbacks	( );
NORTHSTARCPP_SOUND_API	void	dispatch_editor_callbacks	( );

} // namespace sound
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_SOUND_API_H_INCLUDED