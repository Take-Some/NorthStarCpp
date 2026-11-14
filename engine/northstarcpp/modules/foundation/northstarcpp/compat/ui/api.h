#ifndef NORTHSTARCPP_UI_API_H_INCLUDED
#define NORTHSTARCPP_UI_API_H_INCLUDED

#ifndef NORTHSTARCPP_UI_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_UI_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_UI_BUILDING
#			define NORTHSTARCPP_UI_API				NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_UI_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_UI_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_UI_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_UI_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_UI_API


namespace northstarcpp {

namespace input {
	struct world;
} // namespace input

namespace render {
	struct world;
} // namespace input

namespace ui {

struct engine;
struct world;

typedef memory::doug_lea_allocator_type	allocator_type;

NORTHSTARCPP_UI_API	world*	create_world	(engine& engine, input::world& input, render::world& render);
NORTHSTARCPP_UI_API	void	destroy_world	(world*& world);
NORTHSTARCPP_UI_API	void	memory_allocator(allocator_type& allocator);

} // namespace ui
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_UI_API_H_INCLUDED