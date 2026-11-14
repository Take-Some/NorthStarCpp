////////////////////////////////////////////////////////////////////////////
//	Created		: 24.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef OBJECT_CAMERA_H_INCLUDED
#define OBJECT_CAMERA_H_INCLUDED

#include "object_base.h"

namespace northstarcpp{
namespace editor{
class tool_utils;

class object_camera :public object_base
{
	typedef object_base	super;
public:
									object_camera	(tool_utils& t);
	virtual							~object_camera	();
	virtual void					load			(northstarcpp::configs::lua_config_value const& t);
	virtual void					save			(northstarcpp::configs::lua_config_value const& t);
	virtual void					fill_ui			(tree_node^ node, u32& count);
	virtual void					render			(render::base::world& world, float4x4 const& tranform);
	virtual void					initialize_property_holder	();
	
private:
			tool_utils&				m_tool_utils;
			float					m_fov;
			float					m_aspect_ratio;

}; // class object_camera

}// namespace editor
}// namespace northstarcpp

#endif // #ifndef OBJECT_CAMERA_H_INCLUDED