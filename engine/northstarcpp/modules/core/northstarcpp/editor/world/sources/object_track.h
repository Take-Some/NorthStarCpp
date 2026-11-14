////////////////////////////////////////////////////////////////////////////
//	Created		: 27.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef OBJECT_TRACK_H_INCLUDED
#define OBJECT_TRACK_H_INCLUDED

#include "object_base.h"

namespace northstarcpp{
namespace editor{

class tool_utils;

class object_track :public object_base 
{
	typedef object_base		super;
public:
									object_track	(tool_utils& t);
	virtual							~object_track	();
	virtual void					load			(northstarcpp::configs::lua_config_value const& t);
	virtual void					save			(northstarcpp::configs::lua_config_value const& t);
	virtual void					fill_ui			(tree_node^ node, u32& count);
	virtual void					render			(render::base::world& world, float4x4 const& tranform);
	virtual void					initialize_property_holder	();
	
private:
			tool_utils&				m_tool_utils;
}; // class object_track

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef OBJECT_TRACK_H_INCLUDED