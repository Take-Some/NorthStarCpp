//-------------------------------------------------------------------------------------------
//	Created		: 14.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "dialog_node_hypergraph.h"
#include "dialog_graph_node.h"
#include "dialog_links_manager.h"
#include "dialog_graph_link.h"

using northstarcpp::dialog_editor::dialog_node_hypergraph;
using northstarcpp::dialog_editor::dialog_graph_node;
using northstarcpp::dialog_editor::dialog_graph_link;

dialog_node_hypergraph::dialog_node_hypergraph(dialog_graph_node^ owner)
:m_owner(owner)
{
}

dialog_node_hypergraph::~dialog_node_hypergraph()
{
}

void dialog_node_hypergraph::append_node(node^ node)
{
	Controls->Add(node);
	m_nodes->Add(node);
	node->on_added(this);
	node->set_scale(scale);
	links_manager->on_node_added(node);
}

void dialog_node_hypergraph::append_node(nodes^ nds)
{
	for each(northstarcpp::editor::controls::hypergraph::node^ n in nds)
		append_node(n);

	Invalidate(false);
}

void dialog_node_hypergraph::remove_node(node^ node)
{
	Controls->Remove(node);
	m_nodes->Remove(node);
	node->on_removed(this);
	links_manager->on_node_removed(node);
}

void dialog_node_hypergraph::remove_node(nodes^ nds)
{
	for each(northstarcpp::editor::controls::hypergraph::node^ n in nds)
		remove_node(n);

	Invalidate(false);
}

void dialog_node_hypergraph::make_connection(connection_point^ pt_src, connection_point^ pt_dst)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(pt_src);
	NORTHSTARCPP_UNREFERENCED_PARAMETER(pt_dst);
}

dialog_graph_link^ dialog_node_hypergraph::create_connection(node^ source, node^ destination)
{
	dialog_graph_node^ src	= safe_cast<dialog_graph_node^>(source);
	dialog_graph_node^ dst	= safe_cast<dialog_graph_node^>(destination);
	northstarcpp::editor::controls::hypergraph::connection_point^ src_pt = src->get_connection_point("child");
	northstarcpp::editor::controls::hypergraph::connection_point^ dst_pt = dst->get_connection_point("parent");
	if(src_pt==nullptr)
		src_pt = src->add_connection_point(dialog_graph_node::typeid, "child");

	if(dst_pt==nullptr)
		dst_pt = dst->add_connection_point(dialog_graph_node::typeid, "parent");

	src_pt->draw_text = false;
	dst_pt->draw_text = false;
	display_rectangle = display_rectangle;

	return gcnew dialog_graph_link(src_pt, dst_pt, false);
}

void dialog_node_hypergraph::hypergraph_area_MouseDown(Object^ , System::Windows::Forms::MouseEventArgs^ e)
{
	if((ModifierKeys & Keys::Alt)==Keys::Alt)
	{
		if(e->Button==System::Windows::Forms::MouseButtons::Left)
			move_start();

		return;
	}
	super::hypergraph_area_MouseDown(nullptr, e);
}

void dialog_node_hypergraph::hypergraph_area_MouseUp(Object^ , System::Windows::Forms::MouseEventArgs^ e)
{
	if(e->Button==System::Windows::Forms::MouseButtons::Left)
		move_stop();

	super::hypergraph_area_MouseUp(nullptr, e);
}
