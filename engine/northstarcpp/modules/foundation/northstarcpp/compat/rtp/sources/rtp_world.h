////////////////////////////////////////////////////////////////////////////
//	Created		: 03.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RTP_WORLD_H_INCLUDED
#define RTP_WORLD_H_INCLUDED

#include <northstarcpp/compat/rtp/world.h>
#include <northstarcpp/compat/math_randoms_generator.h>

namespace northstarcpp {

namespace animation {
	struct world;
} // namespace animation

namespace input {
	struct world;
} // namespace input

namespace rtp {

//class animation_grasping_controller;
class base_controller;

class rtp_world :
	public northstarcpp::rtp::world,
	private boost::noncopyable
{
public:
				rtp_world	( northstarcpp::rtp::engine& engine  );
	virtual		~rtp_world	( );

private:
virtual		void			render									( northstarcpp::render::debug::renderer& renderer ) const;
virtual		void			tick									( );
virtual		void			dbg_move_control						( float4x4 &view_inverted, input::world&  input );

public:
static		math::random32	&random									( ){ return m_random; }

private:
			void			query_resources							( );
			void			on_resources_loaded						( resources::queries_result& data );
			void			create_test_controllers					( );

			void			load									( );
			void			init									( );
			void			clear_controllers						( );
			void			save_controllers						( const vector< base_controller* > &controllers, bool training );
			void			anim_set_loaded_callback				();

private:
	vector< base_controller*>								m_controllers;

	northstarcpp::animation::world									*m_animation_world;
	northstarcpp::render::debug::renderer&							m_renderer;
	timing::timer											m_timer;
	timing::timer											m_timer_continious;

	pcstr													m_resource_path;
private:
	static math::random32									m_random;

}; // class rtp_world

} // namespace rtp
} // namespace northstarcpp


#endif // #ifndef RTP_WORLD_H_INCLUDED