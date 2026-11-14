////////////////////////////////////////////////////////////////////////////
//	Created		: 18.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NODE_IDENTIFIERS_H_INCLUDED
#define NODE_IDENTIFIERS_H_INCLUDED

#define body_node_type_id			0x00700
#define body_node_type_name			"northstarcpp_body"

#define body_node_manip_type_id		0x00701
#define body_node_manip_type_name	"northstarcpp_bodyManip"


#define collision_node_type_id		0x00600
#define collision_node_type_name	"northstarcpp_collision"

#define joint_node_type_id			0x00800
#define joint_node_type_name		"northstarcpp_constraint"

#define shader_node_type_id			0x00801
#define shader_node_type_name		"northstarcpp_shader"

#define fake_drawing_locator_type_id	0x0010BC08
#define fake_drawing_locator_type_name	"northstarcpp_fake_drawing_locator"

// material
#define shader_name_att					"northstarcpp_render_shader"
#define doublesided_att_name			"double_sided"

MDagPath get_path_by_name		(MString const& name, MStatus& stat);
MObject get_node_by_name		(MString const& name, MStatus& stat);

#endif // #ifndef NODE_IDENTIFIERS_H_INCLUDED