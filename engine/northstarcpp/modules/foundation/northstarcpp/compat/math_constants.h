////////////////////////////////////////////////////////////////////////////
//	Created 	: 14.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_CONSTANTS_H_INCLUDED
#define NORTHSTARCPP_MATH_CONSTANTS_H_INCLUDED

#include <limits>
#include <northstarcpp/math.hpp>

namespace northstarcpp {

template < typename first_type, typename second_type >
union horrible_cast {
	first_type			first;
	second_type			second;

	inline	horrible_cast	( first_type const& first ) :
		first			( first )
	{
	}
}; // union horrible_cast

namespace math {

float const	SNaN		= horrible_cast< int, float > ( 0x7f800001 ).second;	// 0x7f800001..0x7fbfffff and 0xff800001..0xffbfffff
float const	QNaN		= horrible_cast< int, float > ( 0x7fc00001 ).second;	// 0x7fc00001..0x7fffffff and 0xffc00001..0xffffffff

float const	infinity	= horrible_cast< int, float > ( 0x7f800000 ).second;	// -infinity = 0xff800000

float const float_max	=  std::numeric_limits< float >::max( );
float const float_min	= -std::numeric_limits< float >::max( );

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_CONSTANTS_H_INCLUDED