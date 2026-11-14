////////////////////////////////////////////////////////////////////////////
//	Created		: 13.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TOOL_TAB_H_INCLUDED
#define TOOL_TAB_H_INCLUDED

namespace northstarcpp {
	namespace editor {

public interface class tool_tab
{
public:
	System::Windows::Forms::UserControl^	frame () abstract;
}; // class tool_tab

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef TOOL_BASE_TAB_H_INCLUDED