////////////////////////////////////////////////////////////////////////////
//	Created		: 02.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NAVIGATION_CONTROLLER_H_INCLUDED
#define NAVIGATION_CONTROLLER_H_INCLUDED

#include "animation_controller.h"
#include "navigation_action.h"


namespace northstarcpp {
namespace rtp {

class navigation_controller :
	public animation_controller< navigation_action > 
{
	typedef animation_controller< navigation_action > super;

public:
						navigation_controller	( northstarcpp::animation::i_animation_controller_set *set );

public:
	virtual		bool	dbg_update_walk			( float td );
	virtual		void	render					( northstarcpp::render::debug::renderer& renderer ) const;
	virtual	void		dbg_move_control		( float4x4 &view_inverted, input::world& input_world  );
private:
	virtual		pcstr	type					( )const { return "navigation"; }

private:
	virtual		void	from_world				( const world_space_param_type& w, space_param_type &p )const;
	virtual		void	on_new_walk_action		( world_space_param_type& w, space_param_type	&params );

private:
	float2				m_control_path;


private:
	float				m_dbg_cam_height;
	float				m_dbg_cam_dist;

}; // class navigation_controller

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef NAVIGATION_CONTROLLER_H_INCLUDED