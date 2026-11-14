//-------------------------------------------------------------------------------------------
//	Created		: 06.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_EDITOR_RESOURCES_COOKER_H_INCLUDED
#define DIALOG_EDITOR_RESOURCES_COOKER_H_INCLUDED

#include "dialog.h"
#pragma managed(push, off)

#include <northstarcpp/resources_cook_classes.h>

namespace northstarcpp {
namespace dialog_editor {
	class dialog;
	struct dialog_graph_node_layout;

	struct dialog_resources : public resources::unmanaged_resource
	{
				dialog_resources	();
				~dialog_resources	();
		void	save				(pcstr path);
		dialog* get_dialog			();

//		virtual	void	recalculate_memory_usage_impl ( ) { m_memory_usage_self.unmanaged = get_size(); }

		dialog_node_base_ptr*		m_dialog;
		dialog_graph_node_layout*	m_layout;
	}; // struct dialog_resources

	typedef	intrusive_ptr<dialog_resources, resources::unmanaged_intrusive_base, threading::multi_threading_interlocked_policy> dialog_resources_ptr;

	class dialog_editor_resources_cooker : public resources::translate_query_cook
	{
	public:
						dialog_editor_resources_cooker	();
		virtual void	translate_query					(northstarcpp::resources::query_result & parent);
		virtual void	delete_resource					(resources::unmanaged_resource * resource);
				void	on_loaded						(resources::queries_result & result);
	}; // class dialog_editor_resources_cooker
} // namespace dialog_editor
} // namespace northstarcpp
#pragma managed(pop)
#endif // #ifndef DIALOG_EDITOR_RESOURCES_COOKER_H_INCLUDED
