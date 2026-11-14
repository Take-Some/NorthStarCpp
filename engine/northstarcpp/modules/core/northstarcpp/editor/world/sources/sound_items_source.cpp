////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "sound_items_source.h"
#include <northstarcpp/editor/base/managed_delegate.h>
#include "sound_editor.h"

using namespace System::IO;

using northstarcpp::editor::sound_editor;

namespace northstarcpp
{
	namespace editor
	{
		tree_view^			sound_items_source::parent::get			()
		{
			return m_parent;
		}

		void				sound_items_source::parent::set			(tree_view^ value)
		{
			m_parent = value;
		}
	
		void				sound_items_source::refresh				()
		{
			fs_iterator_delegate* q = NEW(fs_iterator_delegate)(gcnew fs_iterator_delegate::Delegate(this, &sound_items_source::on_fs_iterator_ready));
			resources::query_fs_iterator(	unmanaged_string(sound_editor::sound_resources_path+"items").c_str(), 
				boost::bind(&fs_iterator_delegate::callback, q, _1), g_allocator);
		}

		void				sound_items_source::on_fs_iterator_ready	(northstarcpp::resources::fs_iterator fs_it)
		{
			process_fs(fs_it.children_begin(), "");
		}

		void				sound_items_source::process_fs			(northstarcpp::resources::fs_iterator it, String^ file_path)
		{
			while( !it.is_end() )
			{
				if(it.is_folder())
				{
					String^ folder_name = gcnew String(it.get_name());
					m_parent->add_group(file_path+folder_name);
					process_fs	(it.children_begin(), file_path+folder_name+"/");
				}
				else
				{
					if(Path::GetExtension(gcnew String(it.get_name())) == ".sound_item")
					{
						String^ file_name = Path::GetFileNameWithoutExtension(gcnew String(it.get_name()));

						m_parent->add_item(file_path+file_name);
					}
				}
				++it;
			}
		}
	}//namespace editor
}//namespace northstarcpp