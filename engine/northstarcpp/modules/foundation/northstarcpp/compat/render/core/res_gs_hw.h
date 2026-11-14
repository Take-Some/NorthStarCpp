////////////////////////////////////////////////////////////////////////////
//	Created		: 06.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RES_GS_HW_H_INCLUDED
#define RES_GS_HW_H_INCLUDED

#include "res_declaration.h"
#include "res_xs_hw.h"

namespace northstarcpp {
namespace render_dx10 {
	
typedef res_xs_hw<gs_data>	res_gs_hw;
typedef northstarcpp::intrusive_ptr<res_gs_hw, res_base, threading::single_threading_policy>	ref_gs_hw;

} // namespace render
} // namespace northstarcpp

#endif // #ifndef RES_GS_H_INCLUDED