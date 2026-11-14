////////////////////////////////////////////////////////////////////////////
//	Created		: 21.01.2010
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/base/world.h>
#include <northstarcpp/render/base/platform.h>
#include <northstarcpp/memory_macros.h>
#include "command_update_visual_vertex_buffer.h"

using northstarcpp::render::base_world;
using northstarcpp::render::buffer_fragment;
using northstarcpp::render::command_update_visual_vertex_buffer;


command_update_visual_vertex_buffer::command_update_visual_vertex_buffer( base_world& world, northstarcpp::render::visual_ptr const& object, northstarcpp::vectora<buffer_fragment> const& fragments, northstarcpp::memory::base_allocator& allocator)
:m_world		(world),
m_object		(object),
m_allocator		(allocator),
m_fragments		(allocator)
{
	ASSERT(m_object);
	m_fragments.resize( fragments.size());
	Fragments::iterator			it	= m_fragments.begin();
	Fragments::const_iterator	end	= m_fragments.end();

	Fragments::const_iterator it_o	= fragments.begin();

	for( ; it != end; ++it, ++it_o)
	{
		*it = *it_o;
		it->buffer = NORTHSTARCPP_NEW_ARRAY_IMPL( m_allocator, char, it_o->size);
		memory::copy( it->buffer, it_o->size, it_o->buffer, it_o->size);
	}
}

command_update_visual_vertex_buffer::~command_update_visual_vertex_buffer	()
{
	Fragments::iterator			it	= m_fragments.begin();
	Fragments::const_iterator	end	= m_fragments.end();

	for( ; it != end; ++it)
	{
		NORTHSTARCPP_DELETE_ARRAY_IMPL( m_allocator, it->buffer);
	}
}

void command_update_visual_vertex_buffer::execute()
{
	ASSERT								( m_object );
	m_world.platform().update_visual_vertex_buffer	( m_object, m_fragments);
}

