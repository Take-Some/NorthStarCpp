////////////////////////////////////////////////////////////////////////////
//	Created		: 07.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_ANIMATIONS_APPLICATION_H_INCLUDED
#define TEST_ANIMATIONS_APPLICATION_H_INCLUDED

#include <northstarcpp/core/core.h>
//#include "../../animation/sources/animation_data.h"
//#include "anim_track_hermite.h"
//#include "anim_track_discrete.h"
//#include "../../animation/sources/anim_track_discrete_channels.h"
#include "northstarcpp/core/engine.h"

//namespace northstarcpp {
//namespace maya_animation {
//
//class animation_data;
//struct test_skeleton;
//typedef	t_animation_data<anim_track_discrete> animation_data_discret;
//typedef	t_animation_data<anim_track_hermite> animation_data_hermite;
//
//} // namespace animation
//} // namespace northstarcpp

namespace test_animations {

class application : public northstarcpp::core::engine {
public:
			void	initialize		( ){};
			void	execute			( ){};
			void	finalize		( ){};
	virtual void 	exit			( int  ){};
	virtual void 	set_exit_code	( int  ){};
	virtual int  	get_exit_code	( ) const{return 0;};

private:
	virtual	pcstr	get_resource_path		( ) const;
	virtual pcstr	get_mounts_path			( ) const { return get_resource_path(); }
	virtual	pcstr	get_underscore_G_path	( ) const;

private:
	static const	u32	num_data = 3;
	template<class data_type> 
	void test_anim( data_type* data[num_data], pcstr name );
	
private:
#if(0)
	northstarcpp::animation::animation_data*			m_data[num_data];
	northstarcpp::animation::animation_data_discret*	m_data_discret[num_data];
	northstarcpp::animation::animation_data_hermite*	m_data_hermite[num_data];
	northstarcpp::animation::test_skeleton*				m_skel;
	northstarcpp::animation::t_animation_data<northstarcpp::animation::anim_track_discrete_channels> m_data_discret_channels;
#endif
	u32											m_exit_code;
}; // class application

} // namespace test_animations

#endif // #ifndef TEST_ANIMATIONS_APPLICATION_H_INCLUDED