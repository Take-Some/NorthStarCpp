////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BASE_CONTROLLER_H_INCLUDED
#define BASE_CONTROLLER_H_INCLUDED

namespace northstarcpp {

namespace render {
namespace debug {

struct renderer;

} // namespace debug
} // namespace render

namespace animation {
	struct world;
} // namespace animation

namespace rtp {

class base_controller	
{

public:
	virtual						~base_controller( ){ }


	virtual	float				learn_step			( )=0;
	virtual	void				learn				( )=0;
	virtual	void				learn_init			( )=0;
	virtual	void				learn_stop			( )=0;
	virtual	void				step_logic			( bool learn )=0;

	virtual	void				save_training_sets	( northstarcpp::configs::lua_config_value cfg )const=0;
	virtual	void				load_training_sets	( const northstarcpp::configs::lua_config_value &cfg )=0;
	virtual	void				save				( northstarcpp::configs::lua_config_value cfg, bool training_tries )const = 0;
	virtual	void				load				( const northstarcpp::configs::lua_config_value &cfg )=0;
	virtual	pcstr				type				( )const = 0;

	virtual	void				render				( northstarcpp::render::debug::renderer& renderer ) const=0;
	virtual	bool				dbg_update_walk		( float dt ) = 0;
	virtual	void				dbg_move_control	( float4x4 &, input::world&  ) {};
}; // class base_controller



base_controller*	create_controller( pcstr type, animation::world &w );
base_controller*	create_test_grasping_controller(  );
base_controller*	create_animation_grasping_controller( animation::world &w );
base_controller*	create_navigation_controller( animation::world &w );

void				learn_step_controllers(  vector< base_controller* > &controllers );
void				learn_init_controllers(  vector< base_controller* > &controllers );
void				learn_stop_controllers(  vector< base_controller* > &controllers );


} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef BASE_CONTROLLER_H_INCLUDED