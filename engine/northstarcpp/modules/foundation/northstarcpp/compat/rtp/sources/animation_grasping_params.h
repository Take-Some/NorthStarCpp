////////////////////////////////////////////////////////////////////////////
//	Created		: 05.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIMATION_GRASPING_PARAMS_H_INCLUDED
#define ANIMATION_GRASPING_PARAMS_H_INCLUDED

#include "grasping_space_params.h"
#include "animation_action_params.h"
//

namespace northstarcpp {
namespace rtp {





struct animation_grasping_world_params:
	public animation_world_params
{
	animation_grasping_world_params( const float4x4 &pos_controller );
	animation_grasping_world_params(  );

	void	save( northstarcpp::configs::lua_config_value config )const;
	
	void	load( const northstarcpp::configs::lua_config_value &config );

	float4x4 taget_pos;

}; // struct animation_grasping_world_params ;






typedef	 grasping_space_params		animation_grasping_space_params;

inline void	transform_to_param	( const float4x4 &transform,  animation_grasping_space_params& param );





} // namespace rtp
} // namespace northstarcpp



#include "animation_grasping_params_inline.h"

#endif // #ifndef ANIMATION_GARSPING_PARAMS_H_INCLUDED