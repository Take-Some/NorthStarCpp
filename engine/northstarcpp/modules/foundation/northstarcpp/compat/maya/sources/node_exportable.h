////////////////////////////////////////////////////////////////////////////
//	Created		: 23.06.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NODE_EXPORTABLE_H_INCLUDED
#define NODE_EXPORTABLE_H_INCLUDED
//#include <northstarcpp/compat/configs_lua_config_value.h>
#include <northstarcpp/compat/configs.h>
class node_exportable 
{
public:
		MStatus		export_node		( northstarcpp::configs::lua_config_value cfg );
private:
virtual	MStatus		do_export_node		( northstarcpp::configs::lua_config_value cfg ) =0;
protected:

private:
	bool	m_processing;
}; // class node_exportable

#endif // #ifndef NODE_EXPORTABLE_H_INCLUDED