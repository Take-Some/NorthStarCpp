////////////////////////////////////////////////////////////////////////////
//	Created		: 12.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TOOL_SOLID_VISUAL_H_INCLUDED
#define TOOL_SOLID_VISUAL_H_INCLUDED

#include "tool_base.h"

#pragma managed(push,off)
#include <northstarcpp/compat/resources_fs.h>
#include <northstarcpp/compat/configs.h>
#pragma managed(pop)

namespace northstarcpp{
namespace game{
namespace detail{
	class world;
}//namespace detail

class object_base;

class tool_solid_visual :public tool_base
{
	typedef tool_base	super;
public:
						tool_solid_visual		(northstarcpp::game::game_world& w);
	virtual				~tool_solid_visual		();

	virtual object_base* load_object			(northstarcpp::configs::lua_config_value const& t);
	virtual object_base* create_object			(pcstr name);
	virtual object_base* create_raw_object		(pcstr id);
	virtual void		destroy_object			(object_base* o);
	virtual pcstr		type_name				() const					{return "solid_visual";}

}; // class tool_solid_visual

} // namespace game
} // namespace northstarcpp

#endif // #ifndef TOOL_SOLID_VISUAL_H_INCLUDED