////////////////////////////////////////////////////////////////////////////
//	Created		: 06.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/core/res_sampler_list.h>
#include <northstarcpp/render/core/resource_manager.h>

namespace northstarcpp {
namespace render_dx10 {

res_sampler_list::res_sampler_list	( sampler_slots const & slots)
{
	for (u32 i = 0; i < D3D_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
	{
		if( slots[i].slot_id != enum_slot_ind_null)
		{
			resize(i+1, ref_sampler_state (NULL));
			(*this)[i] = slots[i].state;
		}
	}
}

void res_sampler_list::_free () const
{
	resource_manager::ref().release( const_cast<res_sampler_list*>(this));
}

bool res_sampler_list::equal(const res_sampler_list& base) const
{
	if (size() != base.size())
		return false;

	for (u32 cmp = 0; cmp < size(); ++cmp)
	{
		if ((*this)[cmp] != base[cmp])	return false;
	}

	return true;
}

bool res_sampler_list::equal( sampler_slots const & base) const
{
	for (u32 i = 0; i < D3D_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
	{
		if( i < size())
		{
			if ((*this)[i] != base[i].state)	
				return false;
		}
		else if( base[i].slot_id != enum_slot_ind_null)
			return false;
	}

	return true;
}


// res_sampler_list::res_sampler_list()
// {
// //	ZeroMemory( &m_samplers, sizeof(m_samplers));
// }


} // namespace render
} // namespace northstarcpp
