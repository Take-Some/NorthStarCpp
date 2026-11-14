//-------------------------------------------------------------------------------------------
//	Created		: 18.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "dialog_node_connection_editor.h"
#include "dialog_document.h"

using northstarcpp::dialog_editor::dialog_node_connection_editor;
using northstarcpp::dialog_editor::dialog_document;
using northstarcpp::editor::controls::hypergraph::node;

void dialog_node_connection_editor::execute(node^ source, node^ destination)
{
	m_document->add_connection(source, destination);
}
