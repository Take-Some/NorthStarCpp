////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "animation_channel_data.h"

namespace northstarcpp {
namespace animation {

	animation_channel_data::animation_channel_data( )
	{

	}

	void	animation_channel_data::save( northstarcpp::configs::lua_config_value cfg  )const 
	{
		m_bi_spline.save( cfg );
	}

	void	animation_channel_data::load( northstarcpp::configs::lua_config_value const& cfg  )
	{
		m_bi_spline.load ( cfg );
	}

} // namespace animation
} // namespace northstarcpp

