////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_CYLINDER_INLINE_H_INCLUDED
#define NORTHSTARCPP_MATH_CYLINDER_INLINE_H_INCLUDED

namespace northstarcpp {
namespace math {

inline cylinder::cylinder				( )
#ifdef DEBUG
:	radius		( SNaN ),
	height		( SNaN )
#endif // #ifdef DEBUG
{
}

inline bool cylinder::valid		( ) const
{
	return		( center.valid( ) && direction.valid( ) && math::valid( height ) && math::valid( radius ) );
}

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_CYLINDER_INLINE_H_INCLUDED