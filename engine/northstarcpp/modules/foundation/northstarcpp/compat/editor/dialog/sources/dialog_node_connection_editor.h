//-------------------------------------------------------------------------------------------
//	Created		: 18.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

namespace northstarcpp {
namespace dialog_editor {

	ref class dialog_document;

	public ref class dialog_node_connection_editor : public northstarcpp::editor::controls::hypergraph::connection_editor
	{
	public:
						dialog_node_connection_editor(dialog_document^ d):m_document(d)	{};
		virtual void	execute(northstarcpp::editor::controls::hypergraph::node^ src, northstarcpp::editor::controls::hypergraph::node^ dst);

	private:
		dialog_document^ m_document;
	}; // ref class dialog_node_connection_editor
} // namespace dialog_editor
} // namespace northstarcpp
