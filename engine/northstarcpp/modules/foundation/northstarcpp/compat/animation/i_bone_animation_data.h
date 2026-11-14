////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef I_BONE_ANIMATION_DATA_H_INCLUDED
#define I_BONE_ANIMATION_DATA_H_INCLUDED

#include <northstarcpp/compat/animation/i_animation_channel_data.h>
#include <northstarcpp/compat/animation/anim_track_common.h>

namespace northstarcpp {
namespace animation {

class i_animation_channel_data;

struct i_bone_animation_data {
public:
	virtual void							save		( northstarcpp::configs::lua_config_value config  )const		= 0;
	virtual void							load		( northstarcpp::configs::lua_config_value const& config  )		= 0;

public:
	virtual	i_animation_channel_data		&channel	( northstarcpp::animation::enum_channel_id ch )					= 0;
	virtual	const i_animation_channel_data	&channel	( northstarcpp::animation::enum_channel_id ch )const			= 0;
	virtual	bool							compare		( i_bone_animation_data const &d )const					= 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( i_bone_animation_data )
}; // class i_bone_animation_data

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef I_BONE_ANIMATION_DATA_H_INCLUDED