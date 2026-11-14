////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef I_SKELETON_ANIMATION_DATA_H_INCLUDED
#define I_SKELETON_ANIMATION_DATA_H_INCLUDED

#include <northstarcpp/compat/resources.h>

namespace northstarcpp {

namespace configs{
	class lua_config_value;
}

namespace animation {

struct i_bone_animation_data;
class  i_bone_names;

struct i_skeleton_animation_data : public resources::unmanaged_resource 
{
public:
	virtual	void							save						( northstarcpp::configs::lua_config_value &cfg )const		= 0;
	virtual	void							load						( northstarcpp::configs::lua_config_value const &cfg )		= 0;
	virtual i_bone_animation_data			&bone						( u32 id )											= 0;
	virtual const i_bone_animation_data		&bone						( u32 id )const										= 0;
	virtual	u32								num_bones					( )const											= 0;
	virtual	void							set_num_bones				( u32 size )										= 0;
	virtual	bool							compare						( i_skeleton_animation_data const & skel )const		= 0;

public:
	virtual	i_bone_names 					*get_bone_names				( )= 0;

	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( i_skeleton_animation_data )
}; // class i_skeleton_animation_data

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef I_SKELETON_ANIMATION_DATA_H_INCLUDED