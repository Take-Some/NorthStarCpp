//-------------------------------------------------------------------------------------------
//	Created		: 25.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

using namespace System;
using namespace System::Collections;
using namespace System::ComponentModel;

#include "dialog_link.h"

namespace northstarcpp {
namespace dialog_editor {
	ref class dialog_links_manager : public northstarcpp::editor::controls::hypergraph::links_manager
	{
		typedef northstarcpp::editor::controls::hypergraph::node				node;
		typedef northstarcpp::editor::controls::hypergraph::link				link;
		typedef northstarcpp::editor::controls::hypergraph::connection_point	connection_point;
		typedef Generic::List<link^>					links;
		typedef Generic::List<node^>					nodes;

	public:
						dialog_links_manager(northstarcpp::editor::controls::hypergraph::hypergraph_control^ h);
		virtual void	on_node_added		(node^ node);
		virtual void	on_node_removed		(node^ node);
		virtual void	on_node_destroyed	(node^ node);
		virtual void	create_link			(northstarcpp::editor::controls::hypergraph::connection_point^ pt_src, northstarcpp::editor::controls::hypergraph::connection_point^ pt_dst);
		virtual void	destroy_link		(northstarcpp::editor::controls::hypergraph::link^ link);
		virtual links^	visible_links		();
		virtual void	clear				();

	private:
		links^													m_links;
		northstarcpp::editor::controls::hypergraph::hypergraph_control^	m_hypergraph;
	}; // ref class dialog_links_manager
} // namespace dialog_editor
} // namespace northstarcpp