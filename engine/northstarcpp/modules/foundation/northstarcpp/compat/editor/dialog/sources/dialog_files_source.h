//-------------------------------------------------------------------------------------------
//	Created		: 17.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DAILOG_FILES_SOURCE_H_INCLUDED
#define DAILOG_FILES_SOURCE_H_INCLUDED

using namespace System;
using namespace System::Windows::Forms;
using northstarcpp::editor::controls::tree_view_source;
using northstarcpp::editor::controls::tree_view;

namespace northstarcpp {
namespace dialog_editor {
	ref class dialogs_node_sorter : public System::Collections::IComparer 
	{
	public:
		virtual int Compare	(Object^ x, Object^ y);
	}; // ref class dialogs_node_sorter

	ref class dialog_files_source : public northstarcpp::editor::controls::tree_view_source
	{
	public:
		virtual	void			refresh			();
		virtual property	tree_view^			parent
		{
			tree_view^			get				();
			void				set				(tree_view^ value);
		};

	private:
				void	on_fs_iterator_ready	(northstarcpp::resources::fs_iterator fs_it);
				void	process_fs				(northstarcpp::resources::fs_iterator it,  String^ file_path);

	private:
		tree_view^	m_parent;
	}; // class raw_files_source
}//namespace dialog_editor
}//namespace northstarcpp
#endif // #ifndef DAILOG_FILES_SOURCE_H_INCLUDED