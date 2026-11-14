////////////////////////////////////////////////////////////////////////////
//	Created		: 19.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef I_ANIMATION_CHANNEL_DATA_H_INCLUDED
#define I_ANIMATION_CHANNEL_DATA_H_INCLUDED

#include <northstarcpp/compat/animation/i_bi_spline_data.h>

namespace northstarcpp {
namespace animation {

class i_animation_channel_data {

public:
	typedef i_bi_spline_data<float>			i_data_channel_type;

public:
	virtual void							save					( northstarcpp::configs::lua_config_value config  )const =0;
	virtual void							load					( northstarcpp::configs::lua_config_value const& config  ) =0;

public:
	virtual const i_data_channel_type&		bi_spline				()const			=0;
	virtual i_data_channel_type&			bi_spline				()				=0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( i_animation_channel_data )
}; // class i_animation_channel_data

} // namespace animation 
} // namespace northstarcpp 

#endif // #ifndef I_ANIMATION_CHANNEL_DATA_H_INCLUDED