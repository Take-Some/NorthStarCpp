//-------------------------------------------------------------------------------------------
//	Created		: 10.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef EDITOR_CREATOR_H_INCLUDED
#define EDITOR_CREATOR_H_INCLUDED

namespace northstarcpp {
namespace editor_base {interface class editor_base;}
namespace dialog_editor {

	public ref class editor_creator
	{
	public:
		static northstarcpp::editor_base::editor_base^	create_editor			(System::String^ resources_path);
		static void								destroy_dialog_editor	();
		static void								memory_allocator		(allocator_type* allocator);

	private:
		static northstarcpp::editor_base::editor_base^	m_ed;
	}; // ref class editor_creator
} // namespace dialog_editor
} // namespace northstarcpp
#endif // #ifndef NORTHSTARCPP_DIALOG_EDITOR_API_H_INCLUDED