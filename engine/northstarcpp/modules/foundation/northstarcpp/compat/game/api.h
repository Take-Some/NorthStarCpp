////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_GAME_API_H_INCLUDED
#define NORTHSTARCPP_GAME_API_H_INCLUDED

#include <northstarcpp/compat/engine/engine_user.h>	// for northstarcpp::engine_user::allocator_type

#ifndef NORTHSTARCPP_GAME_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_GAME_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_GAME_BUILDING
#			define NORTHSTARCPP_GAME_API			NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_GAME_BUILDING
#			define NORTHSTARCPP_GAME_API			NORTHSTARCPP_DLL_IMPORT
#		endif // #ifdef NORTHSTARCPP_GAME_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_GAME_API


namespace northstarcpp {

namespace engine_user {
	struct engine;
	struct world;
} // namespace engine_user

namespace input {
	struct world;
} // namespace input

namespace render {
	struct world;
} // namespace render

namespace ui {
	struct world;
} // namespace ui

namespace physics {
	struct world;
} // namespace physics

namespace rtp {
	struct world;
} // namespace rtp

namespace animation {
	struct world;
} // namespace animation

namespace sound {
	struct world;
} // namespace sound

} // namespace northstarcpp

namespace stalker2 {
namespace game_module {

NORTHSTARCPP_GAME_API	northstarcpp::engine_user::world*	create_world			( 
												northstarcpp::engine_user::engine& engine, 
												northstarcpp::input::world& input, 
												northstarcpp::render::world& render, 
												northstarcpp::ui::world& ui,
												northstarcpp::physics::world& physics,
												northstarcpp::rtp::world& rtp,
												northstarcpp::animation::world& animation,
												northstarcpp::sound::world& sound
											);

NORTHSTARCPP_GAME_API	void						destroy_world			( northstarcpp::engine_user::world*& world );
NORTHSTARCPP_GAME_API	void						set_memory_allocator	( northstarcpp::engine_user::allocator_type& allocator );

} // namespace game_module
} // namespace stalker2

#endif // #ifndef NORTHSTARCPP_GAME_API_H_INCLUDED