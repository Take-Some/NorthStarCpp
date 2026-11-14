////////////////////////////////////////////////////////////////////////////
//	Created		: 02.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIMATION_ACTION_PARAMS_H_INCLUDED
#define ANIMATION_ACTION_PARAMS_H_INCLUDED

namespace northstarcpp {

namespace animation
{
	class i_animation_controller_set;
} // namespace animation

namespace rtp {

class animation_global_actions_params
{

public:
	animation_global_actions_params( northstarcpp::animation::i_animation_controller_set *set );
	

public:
	northstarcpp::animation::i_animation_controller_set		*m_set;

};

struct animation_world_params
{
	animation_world_params( const float4x4 &pos_controller ): controller_pos(pos_controller)
	{}
	animation_world_params( ):controller_pos(float4x4().identity()){}
	
	void	save( northstarcpp::configs::lua_config_value  )const{};
	
	void	load( const northstarcpp::configs::lua_config_value & ){};

	float4x4 controller_pos;
};

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef ANIMATION_ACTION_PARAMS_H_INCLUDED