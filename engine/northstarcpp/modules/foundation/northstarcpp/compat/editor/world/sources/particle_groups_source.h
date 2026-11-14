////////////////////////////////////////////////////////////////////////////
//	Created		: 20.01.2010
//	Author		: Evgeniy Obertyukh
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PARTICLE_GROUPS_SOURCE_H_INCLUDED
#define PARTICLE_GROUPS_SOURCE_H_INCLUDED

using namespace System;
using namespace System::Windows::Forms;

using northstarcpp::editor::controls::tree_view_source;
using northstarcpp::editor::controls::tree_view;

namespace northstarcpp
{
	namespace editor
	{
		ref class particle_graph_node;

		ref class particle_groups_source : tree_view_source
		{
		private:
			tree_view^						m_parent;

		public:
			virtual property tree_view^		parent
			{
				tree_view^			get	(){return m_parent;};
				void				set	(tree_view^ value){m_parent = value;};
			};
			
		private:
			void	on_fs_iterator_ready	(northstarcpp::resources::fs_iterator fs_it);
			void	process_fs				(northstarcpp::resources::fs_iterator it,  String^ file_path);

		public:
			virtual void refresh();

		}; // class particle_groups_source
	}//namespace editor
}//namespace northstarcpp

#endif // #ifndef PARTICLE_GROUPS_SOURCE_H_INCLUDED