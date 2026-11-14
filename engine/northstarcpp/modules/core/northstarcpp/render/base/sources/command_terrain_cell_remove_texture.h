////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_TERRAIN_CELL_REMOVE_TEXTURE_H_INCLUDED
#define COMMAND_TERRAIN_CELL_REMOVE_TEXTURE_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/visual.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_terrain_cell_remove_texture :
	public engine::command,
	private boost::noncopyable
{
public:
							command_terrain_cell_remove_texture( base_world& world, visual_ptr v, u32 tex_user_id);
	virtual					~command_terrain_cell_remove_texture	(  );
	virtual	void			execute				( );

private:
	base_world&						m_world;
	visual_ptr						m_visual;
	u32								m_tex_user_id;

}; // class command_terrain_cell_remove_texture

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_TERRAIN_CELL_REMOVE_TEXTURE_H_INCLUDED