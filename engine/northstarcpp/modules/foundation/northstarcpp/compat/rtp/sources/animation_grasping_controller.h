////////////////////////////////////////////////////////////////////////////
//	Created		: 22.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RTP_ANIMATION_GRASPING_CONTROLLER_H_INCLUDED
#define NORTHSTARCPP_RTP_ANIMATION_GRASPING_CONTROLLER_H_INCLUDED

#include "animation_controller.h"
#include "animation_grasping_action.h"

namespace northstarcpp {
namespace rtp {

class animation_grasping_controller:
	public animation_controller< animation_grasping_action > 
{
	typedef animation_controller< animation_grasping_action > super;

public:
	animation_grasping_controller ( northstarcpp::animation::i_animation_controller_set *set );

public:
	virtual		bool	dbg_update_walk			( float td );

public:
	virtual	void		render					( northstarcpp::render::debug::renderer& renderer ) const;

private:
	virtual		pcstr	type					( )const { return "animation_grasping"; }

private:
	virtual		void	from_world				( const world_space_param_type& w, space_param_type &p )const;

private:
				void	dbg_set_random_taget	();

private:

}; // class animation_grasping_controller

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RTP_ANIMATION_GRASPING_CONTROLLER_H_INCLUDED