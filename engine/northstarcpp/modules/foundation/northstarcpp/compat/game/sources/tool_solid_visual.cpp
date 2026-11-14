////////////////////////////////////////////////////////////////////////////
//	Created		: 12.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "tool_solid_visual.h"
#include "object_solid_visual.h"

using northstarcpp::game::tool_solid_visual;
using northstarcpp::game::object_base;

tool_solid_visual::tool_solid_visual(northstarcpp::game::game_world& w)
:super		(w)
{
//	load_library	();
}

tool_solid_visual::~tool_solid_visual()							
{
}

object_base* tool_solid_visual::create_object(pcstr name)
{
	return create_raw_object(name);
}

object_base* tool_solid_visual::load_object(northstarcpp::configs::lua_config_value const& t)
{
	pcstr library_name		= t["lib_name"];
	object_base* result		= create_object(library_name);
	result->load			(t);
	return					result;
}

object_base* tool_solid_visual::create_raw_object(pcstr id)
{
	object_solid_visual* result			=  NEW(object_solid_visual)(*this);
	result->set_visual_name				(id);
	result->set_name					(id);
	result->set_lib_name				(id);
	return result;
}

void tool_solid_visual::destroy_object(object_base* o)
{
	DELETE(o);
}

