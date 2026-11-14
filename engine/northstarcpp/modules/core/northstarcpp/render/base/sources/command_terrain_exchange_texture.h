////////////////////////////////////////////////////////////////////////////
//	Created		: 11.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_TERRAIN_EXCHANGE_TEXTURE_H_INCLUDED
#define COMMAND_TERRAIN_EXCHANGE_TEXTURE_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/render/base/common_types.h>

namespace northstarcpp {
namespace render {

class base_world;

class command_terrain_exchange_texture :
	public engine::command,
	private boost::noncopyable
{
public:
							command_terrain_exchange_texture( base_world& world, texture_string const& old_texture, texture_string const& new_texture);
	virtual	void			execute				( );

private:
	base_world&						m_world;
	texture_string					m_old;
	texture_string					m_new;

}; // class command_terrain_exchange_texture

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_TERRAIN_EXCHANGE_TEXTURE_H_INCLUDED