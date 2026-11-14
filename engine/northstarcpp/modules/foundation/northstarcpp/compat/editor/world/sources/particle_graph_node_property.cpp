////////////////////////////////////////////////////////////////////////////
//	Created		: 23.03.2010
//	Author		: 
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "particle_graph_node_property.h"
#include <northstarcpp/compat/editor/base/images/images92x25.h>
#include "particle_graph_node_collection.h"


namespace northstarcpp {
	namespace editor {

		void					particle_graph_node_property::OnPaint			(PaintEventArgs^ e)
		{
			if(m_parent != nullptr)
			{
				ImageList^ particle_node_images = safe_cast<particle_graph_node_style_mgr^>(parent_hypergraph->nodes_style_manager)->images;
				Image^ node_image = particle_node_images->Images[m_image_type];

				e->Graphics->DrawImage(node_image, Drawing::Point(0,0));

				if (m_highlighted)
					e->Graphics->DrawImage(particle_node_images->Images[northstarcpp::editor_base::image_node_highlited], Drawing::Point(0,0));
			}
			else {
				particle_editor^ editor = safe_cast<particle_editor^>(safe_cast<DockContent^>(ParentForm)->DockPanel->Parent->Parent);
				e->Graphics->DrawImage(editor->node_style_mgr->images->Images[m_image_type], Drawing::Point(0,0));		
			}

		super::OnPaint(e);
		}
	} // namespace editor
} // namespace northstarcpp
