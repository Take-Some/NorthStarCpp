////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_CYLINDER_H_INCLUDED
#define NORTHSTARCPP_MATH_CYLINDER_H_INCLUDED

namespace northstarcpp {
namespace math {

class NORTHSTARCPP_CORE_API cylinder {
public:
	float3				center;
	float				radius;
	float3				direction;
	float				height;

public:
	inline				cylinder	( );
	inline	bool		valid		( ) const;

public:
	intersection		intersect	( float3 const& position, float3 const& direction, float& distance ) const;

private:
	u32					intersect	( float3 const& position, float3 const& direction, float ( &t )[ 2 ] ) const;
}; // class cylinder

} // namespace math
} // namespace northstarcpp

#include <northstarcpp/compat/math_cylinder_inline.h>

#endif // #ifndef NORTHSTARCPP_MATH_CYLINDER_H_INCLUDED