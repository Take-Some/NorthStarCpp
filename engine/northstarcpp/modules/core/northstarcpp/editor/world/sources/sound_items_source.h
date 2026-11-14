////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SOUND_ITEMS_SOURCE_H_INCLUDED
#define SOUND_ITEMS_SOURCE_H_INCLUDED

using namespace System;
using namespace System::Windows::Forms;

using northstarcpp::editor::controls::tree_view_source;
using northstarcpp::editor::controls::tree_view;

namespace northstarcpp
{
	namespace editor
	{
		ref class sound_items_source : tree_view_source
		{
		private:
			tree_view^ m_parent;

		public:
			virtual property tree_view^	parent
			{
				tree_view^			get	();
				void				set	(tree_view^ value);
			};

		private:
			void	on_fs_iterator_ready	(northstarcpp::resources::fs_iterator fs_it);
			void	process_fs				(northstarcpp::resources::fs_iterator it,  String^ file_path);

		public:
			virtual void refresh();

		}; // class sound_items_source
	}//namespace editor
}//namespace northstarcpp
#endif // #ifndef SOUND_ITEMS_SOURCE_H_INCLUDED