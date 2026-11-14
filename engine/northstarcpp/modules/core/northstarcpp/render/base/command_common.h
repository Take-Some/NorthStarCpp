////////////////////////////////////////////////////////////////////////////
//	Created		: 16.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_COMMON_COMMON_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_COMMON_COMMON_H_INCLUDED

namespace northstarcpp {
namespace render {

typedef fastdelegate::FastDelegate< void ( u32 )>	command_finished_callback;

} // namespace render 
} // namespace northstarcpp 

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_COMMON_COMMON_H_INCLUDED