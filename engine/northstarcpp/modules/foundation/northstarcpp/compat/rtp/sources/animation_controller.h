////////////////////////////////////////////////////////////////////////////
//	Created		: 02.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIMATION_CONTROLLER_H_INCLUDED
#define ANIMATION_CONTROLLER_H_INCLUDED

#include "controller.h"

namespace northstarcpp {

namespace animation {
class i_animation_controller_set;
class i_animation_action;
} // namespace animation

namespace rtp {


template <class action>
class animation_controller:
	public controller< action >
{

typedef controller< action > super;

public:
	animation_controller ( northstarcpp::animation::i_animation_controller_set *set );

public:
	virtual		bool	dbg_update_walk			( float td );
	virtual		void	render					( northstarcpp::render::debug::renderer& renderer ) const;

public:
				void	debug_test_add_actions	( );

protected:
typename action::world_space_param_type			m_controller_position;
bool											m_debug_wolk_initialized;

protected:

static const u32								start_action_id					= 4;

}; // class animation_controller

} // namespace rtp
} // namespace northstarcpp

#include "animation_controller_inline.h"

#endif // #ifndef ANIMATION_CONTROLLER_H_INCLUDED