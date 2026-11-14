////////////////////////////////////////////////////////////////////////////
//	Created		: 18.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_GRASPING_CONTROLLER_H_INCLUDED
#define TEST_GRASPING_CONTROLLER_H_INCLUDED

#include "test_grasping_action.h"
#include "controller.h"

namespace northstarcpp {
namespace rtp {

class test_grasping_controller :
	public controller< test_grasping_action >
{

public:
						test_grasping_controller();
				void	create_actions			();
				float	learn_step				();

	virtual		bool	dbg_update_walk				( float dt );
				void	dbg_set_random_taget		();

public:
	virtual		void	render					( northstarcpp::render::debug::renderer& renderer ) const ;

public:

/*
	void				save					( northstarcpp::configs::lua_config_value cfg, bool training_tries )const;
	void				load					( const northstarcpp::configs::lua_config_value &cfg );
	void				save_training_sets		( northstarcpp::configs::lua_config_value cfg )const;
	void				load_training_sets		( const northstarcpp::configs::lua_config_value &cfg );
*/

private:
	virtual		pcstr		type				( ) const { return "test_grasping"; }
	virtual		void		from_world			( const world_space_param_type& w, space_param_type &p )const;
	virtual		void		on_new_walk_action	();

private:
	float2								m_taget_position;
	//test_grasping_world_params			m_position;
	global_actions_params< test_grasping_action > m_position;
	u32									m_current_walk_step;

}; // class test_grasping_controller

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef TEST_GRASPING_CONTROLLER_H_INCLUDED