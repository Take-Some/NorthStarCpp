////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.01.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PHYSICS_WORLD_H_INCLUDED
#define PHYSICS_WORLD_H_INCLUDED

#include <northstarcpp/compat/physics/world.h>

namespace northstarcpp {

namespace physics {
	struct engine;
	struct shell;
} // namespace physics

namespace collision {
	struct space_partitioning_tree;
	struct geometry;
	struct geometry_instance;
}//collision


} // namespace northstarcpp
class step_object;
class scheduler;
class shell_static;
class temp_storage_holder;
class physics_world :
	public northstarcpp::physics::world,
	private boost::noncopyable
{

public:
	physics_world	( northstarcpp::physics::engine& engine, northstarcpp::render::debug::renderer& renderer );
	virtual			~physics_world	( );

public:
	virtual	void							tick				( );
	virtual	void							clear_resources		( );
	virtual	non_null<physics::shell>::ptr	create_shell		( const configs::binary_config_value	&cfg );
	virtual	non_null<physics::shell>::ptr	create_shell_static	( const configs::binary_config_value	&cfg );
	virtual	non_null<physics::shell>::ptr	create_shell		( const configs::lua_config_value		&cfg );
	
	virtual	non_null<physics::shell>::ptr	create_shell_static	( non_null<collision::geometry_instance>::ptr g_instance );
	virtual					void			destroy_shell		( non_null<northstarcpp::physics::shell>::ptr  sh	 );

	virtual					void			set_ready			( bool v ){ m_b_ready = v; }
public:
							void			activate_object		( step_object	&so );
private:
			northstarcpp::physics::engine			&m_engine;
	northstarcpp::render::debug::renderer			&m_renderer;

private:	
	non_null<northstarcpp::collision::space_partitioning_tree>::ptr m_space;

private:
		scheduler							*m_sheduler;
mutable	temp_storage_holder					*m_temp_storage_holder;

public:
		temp_storage_holder&				get_temp_storage_holder				( )const { return *m_temp_storage_holder ;}

private:
	virtual	void							render								( northstarcpp::render::debug::renderer& renderer ) const ;

public:
	inline northstarcpp::render::debug::renderer&	renderer						( ) const{ return m_renderer; }
			void							add_to_created_geoms				( northstarcpp::collision::geometry* g );
			void							add_to_created_geometry_instances	( northstarcpp::collision::geometry_instance* g );
private:
	shell_static*							dbg_create_shell_static				( );
	shell_static*							m_shell_static;
	bool									m_b_ready;

	physics::vector< northstarcpp::collision::geometry* >				m_created_geoms;//for destroy
	physics::vector< northstarcpp::collision::geometry_instance* >		m_created_geometry_instances;//for destroy

}; // class world

#endif // #ifndef PHYSICS_WORLD_H_INCLUDED