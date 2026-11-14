////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

using northstarcpp::math::frustum;
using northstarcpp::math::intersection;
using northstarcpp::math::aabb;
using northstarcpp::math::float4x4;
using northstarcpp::math::sphere;
using northstarcpp::math::plane;

frustum::frustum( float4x4 const& transform ) :
	frustum( transform, clip_depth::minus_one_to_one )
{
}

frustum::frustum( float4x4 const& transform, clip_depth depth )
{
	m_planes[ 0 ].plane.vector.x	= -( transform.e03 + transform.e00 );
	m_planes[ 0 ].plane.vector.y	= -( transform.e13 + transform.e10 );
	m_planes[ 0 ].plane.vector.z	= -( transform.e23 + transform.e20 );
	m_planes[ 0 ].plane.vector.w	= -( transform.e33 + transform.e30 );
															   
	m_planes[ 1 ].plane.vector.x	= -( transform.e03 - transform.e00 );
	m_planes[ 1 ].plane.vector.y	= -( transform.e13 - transform.e10 );
	m_planes[ 1 ].plane.vector.z	= -( transform.e23 - transform.e20 );
	m_planes[ 1 ].plane.vector.w	= -( transform.e33 - transform.e30 );
															   
	m_planes[ 2 ].plane.vector.x	= -( transform.e03 - transform.e01 );
	m_planes[ 2 ].plane.vector.y	= -( transform.e13 - transform.e11 );
	m_planes[ 2 ].plane.vector.z	= -( transform.e23 - transform.e21 );
	m_planes[ 2 ].plane.vector.w	= -( transform.e33 - transform.e31 );
															   
	m_planes[ 3 ].plane.vector.x	= -( transform.e03 + transform.e01 );
	m_planes[ 3 ].plane.vector.y	= -( transform.e13 + transform.e11 );
	m_planes[ 3 ].plane.vector.z	= -( transform.e23 + transform.e21 );
	m_planes[ 3 ].plane.vector.w	= -( transform.e33 + transform.e31 );
															   
	m_planes[ 4 ].plane.vector.x	= -( transform.e03 - transform.e02 );
	m_planes[ 4 ].plane.vector.y	= -( transform.e13 - transform.e12 );
	m_planes[ 4 ].plane.vector.z	= -( transform.e23 - transform.e22 );
	m_planes[ 4 ].plane.vector.w	= -( transform.e33 - transform.e32 );
															   
	// The near clip plane is z = 0 in Direct3D and z = -w in the legacy convention.
	float const near_w = depth == clip_depth::zero_to_one ? 0.f : 1.f;
	m_planes[ 5 ].plane.vector.x	= -( near_w * transform.e03 + transform.e02 );
	m_planes[ 5 ].plane.vector.y	= -( near_w * transform.e13 + transform.e12 );
	m_planes[ 5 ].plane.vector.z	= -( near_w * transform.e23 + transform.e22 );
	m_planes[ 5 ].plane.vector.w	= -( near_w * transform.e33 + transform.e32 );

	aabb_plane*	i					= m_planes;
	aabb_plane const* const e		= m_planes + plane_count;
	for ( ; i != e; ++i)
		( *i ).normalize			( );
}
