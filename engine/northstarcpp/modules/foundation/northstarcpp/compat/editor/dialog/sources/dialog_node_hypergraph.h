//-------------------------------------------------------------------------------------------
//	Created		: 14.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

using namespace System;
using namespace System::ComponentModel;
using namespace System::Collections;
using namespace System::Windows::Forms;
using namespace System::Data;
using namespace System::Drawing;

namespace northstarcpp {
namespace dialog_editor {
	ref class dialog_graph_node;
	ref class dialog_graph_link;

	public ref class dialog_node_hypergraph : public northstarcpp::editor::controls::hypergraph::hypergraph_control
	{
	private:
		typedef northstarcpp::editor::controls::hypergraph::hypergraph_control super;
		typedef northstarcpp::editor::controls::hypergraph::connection_point connection_point;
		typedef northstarcpp::editor::controls::hypergraph::node node;
		typedef northstarcpp::editor::controls::hypergraph::link link;
		typedef Generic::List<northstarcpp::editor::controls::hypergraph::node^ > nodes;
		typedef Generic::List<northstarcpp::editor::controls::hypergraph::link^ > links;

	public:
						dialog_node_hypergraph		(dialog_graph_node^ owner);
						~dialog_node_hypergraph		();
		virtual	void	append_node					(node^ node) override;
		void			append_node					(nodes^ nds);
		virtual	void	remove_node					(node^ node) override;
		void			remove_node					(nodes^ nds);
		virtual	void	make_connection				(connection_point^ psrc, connection_point^ pdst) override;
	dialog_graph_link^	create_connection			(node^ source, node^ destination);
		virtual	void	hypergraph_area_MouseDown	(Object^ , System::Windows::Forms::MouseEventArgs^ e) override;
		virtual	void	hypergraph_area_MouseUp		(Object^ , System::Windows::Forms::MouseEventArgs^ e) override;

	private: 
		dialog_graph_node^							m_owner;
	};  // class dialog_node_hypergraph 
} // namespace dialog_editor 
} // namespace northstarcpp

