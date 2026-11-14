////////////////////////////////////////////////////////////////////////////
//	Created		: 28.01.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_UPDATE_VISUAL_VERTEX_BUFFER_H_INCLUDED
#define COMMAND_UPDATE_VISUAL_VERTEX_BUFFER_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/common_types.h>
#include <northstarcpp/render/base/visual.h>
#include <northstarcpp/vectora.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_update_visual_vertex_buffer :
	public engine::command,
	private boost::noncopyable
{
public:
	typedef vectora<buffer_fragment> Fragments;
							command_update_visual_vertex_buffer( base_world& world, northstarcpp::render::visual_ptr const& object, vectora<buffer_fragment> const& fragments,  memory::base_allocator& allocator);
							~command_update_visual_vertex_buffer	(  );
	virtual	void			execute				( );

private:
	base_world&						m_world;
	northstarcpp::render::visual_ptr const& m_object;  
	memory::base_allocator&				m_allocator;
	Fragments							m_fragments;

}; // class command_update_visual_vertex_buffer

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_UPDATE_VISUAL_VERTEX_BUFFER_H_INCLUDED