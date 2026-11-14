////////////////////////////////////////////////////////////////////////////
//	Created		: 23.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef LIGHT_PROPS_H_INCLUDED
#define LIGHT_PROPS_H_INCLUDED

namespace northstarcpp {
namespace render {

enum light_type
{
	light_type_direct		= 0,
	light_type_point		= 1,
	light_type_spot			= 2,
	light_type_omnipart		= 3,
	light_type_reflected	= 4,
};


class light_props {
public:

	float4x4		transform;
	float			range;
	float			brightness;
	bool			shadow_cast;
	u32				color;
	light_type		type;
	
}; // class light_props

} // namespace render
} // namespace northstarcpp

#endif // #ifndef LIGHT_PROPS_H_INCLUDED