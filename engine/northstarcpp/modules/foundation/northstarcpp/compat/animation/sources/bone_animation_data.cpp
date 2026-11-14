////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "bone_animation_data.h"

namespace northstarcpp {
namespace animation {

animation_channel_data&	bone_animation_data::channel( enum_channel_id ch )
{
	return m_data[ ch ]; 
}

animation_channel_data const& bone_animation_data::channel( enum_channel_id ch )const
{
	return m_data[ ch ];
}

void	bone_animation_data::save( northstarcpp::configs::lua_config_value config  )const 
{
	for(u32 i = channel_translation_x ; i < channel_max; ++i )
		channel((enum_channel_id)(i)).save( config[i] );
}

 void bone_animation_data::load		( northstarcpp::configs::lua_config_value const& config  )
{
	for(u32 i = channel_translation_x ; i < channel_max; ++i )
		channel((enum_channel_id)(i)).load( config[i] );
}

bool	bone_animation_data::compare( i_bone_animation_data const &d )const
{
	return compare( static_cast_checked< bone_animation_data const& >( d ) );
}

bool	bone_animation_data::compare( bone_animation_data const& d )const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( d );
	for(u32 i = channel_translation_x ; i < channel_max; ++i )
	{
		
		bi_spline_data<float> const&ch_d0 = channel( enum_channel_id(i) ).bi_spline();
		bi_spline_data<float> const&ch_d1 = d.channel( enum_channel_id(i) ).bi_spline();
		if( !ch_d0.compare( ch_d1 ) )
			return false;
	}
	return true;
}

} // namespace animation
} // namespace northstarcpp