////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_SPHERE_INLINE_H_INCLUDED
#define NORTHSTARCPP_MATH_SPHERE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace math {

sphere::sphere				( )
#ifdef DEBUG
:	vector			( float4() )
#endif // #ifdef DEBUG
{
}

sphere::sphere				( float3 const& center, type radius ) :
	center			( center ),
	radius			( radius )
{
}

bool sphere::intersect_ray	( float3 const& position, float3 const& direction ) const
{
	float3 const	position2center = center - position;
	return			( sqr( radius ) + sqr( position2center | direction ) > sqr( position2center.magnitude( ) ) );
}

bool sphere::intersect		( sphere const& other ) const
{
	return			( ( center - other.center ).square_magnitude( ) > sqr( radius + other.radius ) );
}

bool sphere::contains		( float3 const& position )
{
	return			( ( center - position ).square_magnitude( ) < sqr( radius ) );
}

bool sphere::contains		( sphere const& other )
{
	type const		radius_difference = radius - other.radius;
	if ( radius_difference < 0.f )
		return		( false );

	return			( ( center - other.center ).square_magnitude( ) < sqr( radius_difference ) );
}

bool sphere::similar			( sphere const& other ) const
{
	return			( vector.similar( other.vector ) );
}

bool sphere::valid			( ) const
{
	return			( vector.valid( ) );
}

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_SPHERE_INLINE_H_INCLUDED