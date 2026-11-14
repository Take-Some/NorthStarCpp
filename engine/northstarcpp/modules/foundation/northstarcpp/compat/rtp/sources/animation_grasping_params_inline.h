////////////////////////////////////////////////////////////////////////////
//	Created		: 05.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIMATION_GRASPING_PARAMS_INLINE_H_INCLUDED
#define ANIMATION_GRASPING_PARAMS_INLINE_H_INCLUDED

namespace northstarcpp {
namespace rtp {

	inline  animation_grasping_world_params::animation_grasping_world_params(  const float4x4 &pos_controller ): 
		animation_world_params( pos_controller ),
		taget_pos		( float4x4().identity() )
	{

	}
	inline  animation_grasping_world_params::animation_grasping_world_params( ):
		animation_world_params	(),
		taget_pos ( float4x4().identity() )
	{

	}

	inline void	animation_grasping_world_params::save( northstarcpp::configs::lua_config_value config )const
	{
		config[0] = taget_pos.i;
		config[1] = taget_pos.j;
		config[2] = taget_pos.k;
		config[3] = taget_pos.c;
		
		//config[0] = controller_pos.i;
		//config[1] = controller_pos.j;
		//config[2] = controller_pos.k;
		//config[3] = controller_pos.c;

	}
	inline void	animation_grasping_world_params::load( const northstarcpp::configs::lua_config_value &config )
	{
 		taget_pos.i = config[0] ;
 		taget_pos.j = config[1] ;
 		taget_pos.k = config[2] ;
 		taget_pos.c = config[3] ;

	
		//controller_pos.i	=config[0];
		//controller_pos.j	=config[1];
		//controller_pos.k	=config[2];
		//controller_pos.c	=config[3];

	}


	inline void	transform_to_param	( const float4x4 &transform,  animation_grasping_space_params& param )
	{
		float4x4 itransform; 
		//ASSERT_U(  );
		itransform.try_invert( transform );
		float3 tp ( param.taget_position().x, 0,  param.taget_position().y );
		tp = itransform.transform_position( tp );
		param.taget_position()= float2( tp.x, tp.z );
	}

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef ANIMATION_GARSPING_PARAMS_INLINE_H_INCLUDED