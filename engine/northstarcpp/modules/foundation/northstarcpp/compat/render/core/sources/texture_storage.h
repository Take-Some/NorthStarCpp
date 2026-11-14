////////////////////////////////////////////////////////////////////////////
//	Created		: 04.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEXTURE_STORAGE_H_INCLUDED
#define TEXTURE_STORAGE_H_INCLUDED

#include <northstarcpp/compat/render/core/sources/texture_pool.h>
namespace northstarcpp {
namespace render_dx10 {


class texture_storage 
{
public:
	void initialize( northstarcpp::configs::lua_config_value  const & initial_params, u32 memory_amount, D3D_USAGE usage);
	texture_storage();
	~texture_storage();

	res_texture*	get		( u32 width, u32 height, DXGI_FORMAT format);
	void			release	( res_texture* texture);

private:

	typedef map<u32, texture_pool*> pools;
	pools	m_pools;
	bool	m_initialized;

}; // class texture_storage


} // namespace render_dx10
} // namespace northstarcpp

#endif // #ifndef TEXTURE_STORAGE_H_INCLUDED