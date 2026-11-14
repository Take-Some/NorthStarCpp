////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ANIMATION_WORLD_H_INCLUDED
#define NORTHSTARCPP_ANIMATION_WORLD_H_INCLUDED

namespace northstarcpp {

namespace render {
namespace debug {

	struct renderer;

} // namespace render
} // namespace northstarcpp

namespace animation {

struct handler;
struct i_skeleton_animation_data;
class  i_animation_controller_set;

struct NORTHSTARCPP_NOVTABLE world {

	virtual	void						tick							( ) = 0;
	virtual	void						render							( northstarcpp::render::debug::renderer& renderer ) const	=0;
	virtual i_skeleton_animation_data	*create_skeleton_animation_data	( u32 num_bones ) = 0;
	virtual	void						destroy							( i_skeleton_animation_data	* &p ) =0;

	virtual	i_animation_controller_set	*create_controller_set			( configs::lua_config_value	const& cfg ) = 0;
	virtual	void						destroy_controller_set			( i_animation_controller_set* &set ) = 0;
/////////////////////////////////////
	virtual i_animation_controller_set	*dbg_tmp_controller_set			( ) =0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ANIMATION_WORLD_H_INCLUDED