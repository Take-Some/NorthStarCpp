////////////////////////////////////////////////////////////////////////////
//	Created		: 31.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

namespace northstarcpp {
namespace render_dx10 {


constant_host::constant_host (shared_string name): 
name	(name),
type	(rc_INVALID)
{
	vs_slot.reset();
	ps_slot.reset();
	ZeroMemory( update_markers, sizeof(update_markers));
}

} // namespace render
} // namespace northstarcpp
