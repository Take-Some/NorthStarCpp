////////////////////////////////////////////////////////////////////////////
//	Created		: 04.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEXTURE_POOL_H_INCLUDED
#define TEXTURE_POOL_H_INCLUDED

#include <northstarcpp/compat/render/core/res_texture.h>

namespace northstarcpp {
namespace render_dx10 {

class res_texture;

class texture_pool	
{
	struct slot
	{
		slot( res_texture* texture): occupied( false), texture (texture) 
		{}

		bool			occupied;
		res_texture*	texture;
	};

public:

	texture_pool	( u32 width, u32 height, DXGI_FORMAT format, u32 mips, u32 count, D3D_USAGE usage);
	~texture_pool	();

	res_texture*	get					();
	void			release				( res_texture* texture);

	u32				memory_usage		()			{ return m_memory_usage;}
	u32				unoccupied_count	()			{ return m_unoccupied_count;}

private:

	vector<slot>	m_textures;
	u32				m_unoccupied_count;

	u32				m_width;
	u32				m_height;
	DXGI_FORMAT		m_format;
	u32				m_mips;
	u32				m_memory_usage;

}; // class texture_pool


} // namespace render_dx10
} // namespace northstarcpp

#endif // #ifndef TEXTURE_POOL_H_INCLUDED