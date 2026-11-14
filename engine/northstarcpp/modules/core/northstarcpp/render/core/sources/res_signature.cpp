////////////////////////////////////////////////////////////////////////////
//	Created		: 28.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/render/core/res_signature.h>
#include <northstarcpp/render/core/resource_manager.h>

namespace northstarcpp {
namespace render_dx10 {

void res_signature::_free() const
{
	resource_manager::ref().release( const_cast<res_signature*>(this) );
}

} // namespace render
} // namespace northstarcpp
