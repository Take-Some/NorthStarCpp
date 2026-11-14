////////////////////////////////////////////////////////////////////////////
//	Created		: 06.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GRASPING_SPACE_PARAMS_H_INCLUDED
#define GRASPING_SPACE_PARAMS_H_INCLUDED

#include "space_params.h"

#include <northstarcpp/math_randoms_generator.h>

namespace northstarcpp {

namespace configs {
	class lua_config_value;
} // namespace configs

namespace rtp {

struct test_grasping_world_params
{

			test_grasping_world_params	(const float2 &dir_, const float2 &pos_ );
			test_grasping_world_params	();

	void	transform					(  float rotate, const float2 &translate );
	void	save						( northstarcpp::configs::lua_config_value cfg )const;
	void	load						( const northstarcpp::configs::lua_config_value &cfg );
	


	float2	dir;
	float2	pos;
};

class grasping_space_params;

class grasping_space_params:
	public space_param< 2, grasping_space_params >

{
public:
	typedef	space_param< 2, grasping_space_params > super;


//static	const float2 bmin ;
//static	const float2 bmax ;

public:
	grasping_space_params	( const float2 &v ){ taget_position	( )	 = v; }
	grasping_space_params	( ) { taget_position( ) = float2( math::float_max, math::float_max ); }


	const	float2&		taget_position	( ) const	{ return	*( (const float2*)&params ); }
			float2&		taget_position	( )			{ return	*( ( float2*)&params ); }




public:
	bool	similar		( const grasping_space_params& other ) const;
	void	render		( const grasping_space_params& to,  northstarcpp::render::debug::renderer& renderer, u32 color = math::color_xrgb(  255,  255, 255 ) )const;

public:

	void	transform		(  float rotate, const float2 &translate );

public:
	void	save				( northstarcpp::configs::lua_config_value cfg )const;
	void	load				( const northstarcpp::configs::lua_config_value &cfg );



}; // class grasping_space_params

float3 render_pos(const float2& pos );

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef GRASPING_SPACE_PARAMS_H_INCLUDED