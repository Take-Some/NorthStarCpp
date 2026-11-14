////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_TERRAIN_ADD_CELL_H_INCLUDED
#define COMMAND_TERRAIN_ADD_CELL_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>
#include <northstarcpp/compat/render/base/common_types.h>
#include <northstarcpp/compat/render/base/command_common.h>
#include <northstarcpp/compat/render/base/visual.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_terrain_add_cell :
	public engine::command,
	private boost::noncopyable
{
public:
	typedef vectora<buffer_fragment> Fragments;
							command_terrain_add_cell	( base_world& world, visual_ptr v, bool beditor );

	virtual					~command_terrain_add_cell	( );
	virtual	void			execute						( );

private:

	base_world&							m_world;
	visual_ptr							m_visual;
	bool								m_beditor;
}; // class command_terrain_add_cell

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_TERRAIN_ADD_CELL_H_INCLUDED