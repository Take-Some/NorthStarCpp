//-------------------------------------------------------------------------------------------
//	Created		: 25.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "dialog_links_manager.h"
#include "dialog_graph_link.h"
#include "dialog_hypergraph.h"

using northstarcpp::dialog_editor::dialog_links_manager;
using northstarcpp::dialog_editor::dialog_graph_link;
using northstarcpp::editor::controls::hypergraph::hypergraph_control;

dialog_links_manager::dialog_links_manager(hypergraph_control^ h)
:m_hypergraph(h)
{
	m_links = gcnew links;
}

void dialog_links_manager::create_link(connection_point^ pt_src, connection_point^ pt_dst)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(pt_src);
	NORTHSTARCPP_UNREFERENCED_PARAMETER(pt_dst);
	//dialog_graph_link^ lnk = gcnew dialog_graph_link(pt_src, pt_dst);
	//m_links->Add(lnk);
}

void dialog_links_manager::destroy_link(link^ link)
{
	m_links->Remove(link);
}

void dialog_links_manager::on_node_added(node^ node)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(node);
}

void dialog_links_manager::on_node_removed(node^ node)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(node);
}

void dialog_links_manager::on_node_destroyed(node^ node)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(node);
}

dialog_links_manager::links^ dialog_links_manager::visible_links()
{
	return m_links;
}

void dialog_links_manager::clear()
{
	m_links->Clear();
}