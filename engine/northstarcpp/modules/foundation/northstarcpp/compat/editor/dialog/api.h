////////////////////////////////////////////////////////////////////////////
//	Created		: 26.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DIALOG_EDITOR_API_H_INCLUDED
#define NORTHSTARCPP_DIALOG_EDITOR_API_H_INCLUDED

#define NORTHSTARCPP_DIALOG_EDITOR_API

namespace northstarcpp {
namespace dialog_editor {
	typedef memory::doug_lea_allocator_type allocator_type;

} // namespace dialog_editor
} // namespace northstarcpp

#pragma make_public (northstarcpp::dialog_editor::allocator_type)

#endif // #ifndef NORTHSTARCPP_DIALOG_EDITOR_API_H_INCLUDED