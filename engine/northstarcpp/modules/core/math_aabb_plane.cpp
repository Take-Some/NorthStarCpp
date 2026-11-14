////////////////////////////////////////////////////////////////////////////
//	Created		: 18.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/math_aabb_plane.h>
#include <cmath>

using northstarcpp::math::aabb_plane;
using northstarcpp::math::intersection;
using northstarcpp::math::aabb;

static u32 const min_x	= 0;
static u32 const min_y	= 1;
static u32 const min_z	= 2;
static u32 const max_x	= 3;
static u32 const max_y	= 4;
static u32 const max_z	= 5;
static u32 const aabb_lut[8][6]	= {
	{ max_x, max_y, max_z, min_x, min_y, min_z}, 
	{ max_x, max_y, min_z, min_x, min_y, max_z}, 
	{ max_x, min_y, max_z, min_x, max_y, min_z}, 
	{ max_x, min_y, min_z, min_x, max_y, max_z}, 
	{ min_x, max_y, max_z, max_x, min_y, min_z}, 
	{ min_x, max_y, min_z, max_x, min_y, max_z}, 
	{ min_x, min_y, max_z, max_x, max_y, min_z}, 
	{ min_x, min_y, min_z, max_x, max_y, max_z}
};

void aabb_plane::normalize		( )
{
	plane.vector		*= 1.f / plane.normal.magnitude( );
	
	u32 const x = static_cast<u32>( std::signbit( plane.normal.x ) ) << 2;
	u32 const y = static_cast<u32>( std::signbit( plane.normal.y ) ) << 1;
	u32 const z = static_cast<u32>( std::signbit( plane.normal.z ) );

	m_lut_id			= x | y | z;
}

intersection aabb_plane::test	( aabb const& aabb ) const
{
	typedef u32			lut_ids_type[6];
	lut_ids_type const&	ids = aabb_lut[m_lut_id];

	float const values[]{ aabb.min.x, aabb.min.y, aabb.min.z,
		aabb.max.x, aabb.max.y, aabb.max.z };

	// test negative first
	if ( plane.classify( float3( values[ ids[3] ], values[ ids[4] ], values[ ids[5] ] ) ) > 0.f )
		return			(intersection_outside);

	// test positive
	if ( plane.classify( float3( values[ ids[0] ], values[ ids[1] ], values[ ids[2] ] ) ) <= 0.f )
		return			(intersection_inside);

	return				(intersection_intersect);
}