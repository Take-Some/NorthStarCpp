////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.01.2008
//	Author		: Kayla Verner
//	Description : default world interface
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_PHYSICS_WORLD_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_WORLD_H_INCLUDED

namespace northstarcpp {

namespace render {
namespace debug{
	struct renderer;
} // namespace debug
} // namespace render

namespace configs {
	class binary_config_value;
	class lua_config_value;
} // namespace configs

namespace collision {
	struct geometry_instance;
} // namespace collision

namespace physics {

struct handler;
struct shell;

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void						tick				( )																= 0;
	virtual	void						clear_resources		( )																= 0;
	virtual	non_null<shell>::ptr		create_shell		( const configs::binary_config_value	&cfg )					= 0;
	virtual	non_null<shell>::ptr		create_shell		( const configs::lua_config_value		&cfg )					= 0;

	virtual					void		destroy_shell		( non_null<shell>::ptr  sh	 )									= 0;
	virtual	non_null<shell>::ptr		create_shell_static	( const configs::binary_config_value	&cfg )					= 0;
	virtual	non_null<shell>::ptr		create_shell_static	( non_null<northstarcpp::collision::geometry_instance>::ptr g_instance )= 0;
	virtual	void						render				( northstarcpp::render::debug::renderer& renderer ) const			= 0;

	virtual					void		set_ready			( bool v )														= 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace physics
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_PHYSICS_WORLD_H_INCLUDED