////////////////////////////////////////////////////////////////////////////
//	Created		: 20.01.2010
//	Author		: 
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "particle_node_connection_editor.h"
#include "particle_graph_document.h"

using northstarcpp::editor::particle_node_connection_editor;
using northstarcpp::editor::particle_graph_document;
using northstarcpp::editor::controls::hypergraph::node;

void particle_node_connection_editor::execute(node^ source, node^ destination)
{
	m_document->add_connection(source, destination);
}
