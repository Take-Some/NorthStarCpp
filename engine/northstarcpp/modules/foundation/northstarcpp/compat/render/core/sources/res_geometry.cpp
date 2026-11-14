////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/render/core/backend.h>
#include <northstarcpp/compat/render/core/res_geometry.h>
#include <northstarcpp/compat/render/core/resource_manager.h>

namespace northstarcpp {
namespace render_dx10 {

res_geometry::~res_geometry()
{

}

void res_geometry::_free() const
{
	resource_manager::ref().release( const_cast<res_geometry*>(this));
}

bool res_geometry::equal(const res_geometry& other) const
{
	return m_vb==other.m_vb && m_ib==other.m_ib &&
		m_dcl==other.m_dcl && m_vb_stride==other.m_vb_stride;
}

void res_geometry::apply()
{
	backend::ref().set_declaration( m_dcl.c_ptr());
	backend::ref().set_vb( m_vb.c_ptr(), m_vb_stride);
	backend::ref().set_ib( m_ib.c_ptr());
}


} // namespace render
} // namespace northstarcpp
