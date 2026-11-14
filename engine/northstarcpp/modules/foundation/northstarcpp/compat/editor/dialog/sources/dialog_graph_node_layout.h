//-------------------------------------------------------------------------------------------
//	Created		: 07.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_GRAPH_NODE_LAYOUT_H_INCLUDED
#define DIALOG_GRAPH_NODE_LAYOUT_H_INCLUDED

#pragma managed(push, off)

namespace northstarcpp {
namespace dialog_editor {
	struct dialog_graph_node_layout
	{
		typedef associative_vector<u32, math::int2, vector> layout_vector;

					dialog_graph_node_layout();
		void		load					(configs::lua_config_value cfg);
		void		save					(configs::lua_config_value cfg);

		layout_vector	m_layout;
	}; // ref class dialog_graph_node_layout
}; //namespace dialog_editor
}; //namespace northstarcpp
#pragma managed(pop)
#endif // #ifndef DIALOG_GRAPH_NODE_LAYOUT_H_INCLUDED
