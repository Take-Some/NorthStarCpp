////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ANIMATION_CHANNEL_DATA_H_INCLUDED
#define ANIMATION_CHANNEL_DATA_H_INCLUDED
#include "bi_spline_data.h"

#include <northstarcpp/configs_lua_config.h>
#include <northstarcpp/animation/i_animation_channel_data.h>	

namespace northstarcpp {
namespace animation {

class animation_channel_data:
	public i_animation_channel_data
{
public:
			animation_channel_data	( );

public:
	virtual void							save					( northstarcpp::configs::lua_config_value config  )const ;
	virtual void							load					( northstarcpp::configs::lua_config_value const& config  );

public:
	virtual const bi_spline_data< float >	&bi_spline				( )const	{ return m_bi_spline; }
	virtual bi_spline_data< float >			&bi_spline				( )			{ return m_bi_spline; }

private:
	bi_spline_data< float >	m_bi_spline;

}; // class animation_channel_data

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef ANIMATION_CHANNEL_DATA_H_INCLUDED