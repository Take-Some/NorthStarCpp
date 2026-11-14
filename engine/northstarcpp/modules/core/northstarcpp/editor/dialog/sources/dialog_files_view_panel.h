//-------------------------------------------------------------------------------------------
//	Created		: 17.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_FILES_VIEW_PANEL_H_INCLUDED
#define DIALOG_FILES_VIEW_PANEL_H_INCLUDED

using namespace System;
using namespace System::ComponentModel;
using namespace System::Collections;
using namespace System::Windows::Forms;
using namespace System::Data;
using namespace System::Drawing;
using namespace northstarcpp::editor::controls;

namespace northstarcpp {
namespace dialog_editor {
	public ref class dialog_files_view_panel :  public northstarcpp::editor::controls::file_view_panel_base
	{
		typedef northstarcpp::editor::controls::file_view_panel_base super;
		System::Windows::Forms::ImageList^ tree_view_image_list;
		northstarcpp::editor::controls::tree_node^ m_copied_node;

	public:
		dialog_files_view_panel(northstarcpp::editor::controls::document_editor_base^ de):northstarcpp::editor::controls::file_view_panel_base(de)
		{
			in_constructor();
			HideOnClose = true;
		}
		virtual	void	track_active_item				(System::String^ path) override;
		virtual	void	remove_node						(System::Object^ sender, northstarcpp::editor::tree_view_event_args^ e) override;
		virtual	void	copy							(bool del) override;
		virtual	void	paste							() override;

		property northstarcpp::editor::controls::tree_node^ copied_node
		{
				void	set								(northstarcpp::editor::controls::tree_node^ n)	{m_copied_node=n;};
				northstarcpp::editor::controls::tree_node^ 	get	()										{return m_copied_node;};
		};

	protected:
						~dialog_files_view_panel		()	{};
				void	in_constructor					();
		virtual	void	on_node_double_click			(System::Object^ sender, System::Windows::Forms::TreeNodeMouseClickEventArgs^ e) override;
		virtual	void	on_node_key_down				(System::Object^ sender, System::Windows::Forms::KeyEventArgs^ e) override;
		virtual	void	add_new_folder					(System::Object^ sender, northstarcpp::editor::tree_view_event_args^ e) override;
		virtual	void	add_new_file					(System::Object^ sender, northstarcpp::editor::tree_view_event_args^ e) override;
		virtual	void	on_node_name_edit				(System::Object^ sender, System::Windows::Forms::NodeLabelEditEventArgs^ e) override;
		virtual	void	on_node_drag_over				(System::Object^ sender, System::Windows::Forms::DragEventArgs^ e) override;
		virtual	void	on_node_drag_drop				(System::Object^ sender, System::Windows::Forms::DragEventArgs^ e) override;
		virtual	void	on_tree_mouse_down				(System::Object^ sender, System::Windows::Forms::MouseEventArgs^ e) override;
		virtual	void	on_tree_mouse_move				(System::Object^ sender, System::Windows::Forms::MouseEventArgs^ e) override;

	private:
				bool	copy_node_impl					(northstarcpp::editor::controls::tree_node^ s, northstarcpp::editor::controls::tree_node^ d, bool del_old);
	}; // ref class dialog_files_view_panel
} // namespace dialog_editor
} // namespace northstarcpp
#endif // #ifndef DIALOG_FILES_VIEW_PANEL_H_INCLUDED