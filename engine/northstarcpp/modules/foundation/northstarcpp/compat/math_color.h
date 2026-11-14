////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_COLOR_H_INCLUDED
#define NORTHSTARCPP_MATH_COLOR_H_INCLUDED

namespace northstarcpp {
namespace math {

class color : public float4 {
public:
	inline			color	( );
	inline			color	( color const& other );
	inline			color	( float4 const& other );
	inline			color	( type r, type g, type b, type a = 1.f );
	inline			color	( u32 r, u32 g, u32 b, u32 a = 0xff );
	inline			color	( u32 argb );
	inline u32		get_d3dcolor( ) const;
private:
	typedef float4	super;
}; // class color

inline u32 color_argb		( u8 alpha, u8 red, u8 green, u8 blue );
inline u32 color_xrgb		( u8 red, u8 green, u8 blue );

} // namespace math
} // namespace northstarcpp

#include <northstarcpp/compat/math_color_inline.h>

#endif // #ifndef NORTHSTARCPP_MATH_COLOR_H_INCLUDED