////////////////////////////////////////////////////////////////////////////
//	Created		: 23.06.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "node_exportable.h"

MStatus	node_exportable::export_node( northstarcpp::configs::lua_config_value  cfg )
{
	m_processing = true;
	MStatus stat = do_export_node( cfg );
	m_processing = false;
	return stat;
}