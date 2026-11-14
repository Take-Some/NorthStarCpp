////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BONE_ANIMATION_DATA_H_INCLUDED
#define BONE_ANIMATION_DATA_H_INCLUDED

#include "animation_channel_data.h"
#include <northstarcpp/animation/anim_track_common.h>
#include <northstarcpp/animation/i_bone_animation_data.h>

namespace northstarcpp {
namespace animation {

class bone_animation;

class bone_animation_data:
	public	i_bone_animation_data
{

public:
	virtual	animation_channel_data&				channel			( enum_channel_id ch );
	virtual animation_channel_data const&		channel			( enum_channel_id ch )const ;

public:
	virtual void								save			( northstarcpp::configs::lua_config_value config  )const ;
	virtual void								load			( northstarcpp::configs::lua_config_value const& config  );
	virtual	bool								compare			( i_bone_animation_data const &d )const;
			bool								compare			( bone_animation_data const &d )const;
		
private:
	animation_channel_data m_data[ channel_max ];

}; // class bone_animation_data

} // namespace animation
} // namespace northstarcpp
#endif // #ifndef BONE_ANIMATION_DATA_H_INCLUDED