//-------------------------------------------------------------------------------------------
//	Created		: 17.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

using namespace System;
using namespace System::ComponentModel;
using namespace System::Collections;
using namespace System::Windows::Forms;
using namespace System::Data;
using namespace System::Drawing;

namespace northstarcpp {
namespace dialog_editor {
	ref class northstarcpp::editor::controls::drag_cursor_collection;

	public ref class dialog_control_panel : public northstarcpp::editor::controls::toolbar_panel_base
	{
		typedef northstarcpp::editor::controls::toolbar_panel_base super; 
	public:
		dialog_control_panel(northstarcpp::editor::controls::document_editor_base^ ed):northstarcpp::editor::controls::toolbar_panel_base(ed)
		{
			in_constructor();
		}

	protected:
		~dialog_control_panel()
		{
			if(components)
				delete components;
		}

		virtual void		OnGiveFeedback	(GiveFeedbackEventArgs^ e) override;

	private:
		void				in_constructor			();
		System::Void		single_node_mouse_down	(System::Object^ sender, System::Windows::Forms::MouseEventArgs^ e);
		System::Void		quest_answ_mouse_down	(System::Object^ sender, System::Windows::Forms::MouseEventArgs^ e);

	private: 
		System::Windows::Forms::PictureBox^	pictureBox1;
		System::Windows::Forms::PictureBox^	pictureBox2;
		northstarcpp::editor::controls::drag_cursor_collection^	m_cursors;
	}; // ref class dialog_control_panel
} // namespace dialog_editor
} // namespace northstarcpp