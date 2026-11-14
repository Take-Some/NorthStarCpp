////////////////////////////////////////////////////////////////////////////
//	Created		: 05.05.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "colliders_ray_test_geometry.h"
#include "triangle_mesh_base.h"
#include "opcode_include.h"

using northstarcpp::collision::colliders::ray_test_geometry;
using northstarcpp::collision::triangle_mesh_base;
using northstarcpp::collision::ray_triangles_type;
using northstarcpp::collision::triangles_predicate_type;
using northstarcpp::collision::object;
using northstarcpp::math::float3;

ray_test_geometry::ray_test_geometry	(
		triangle_mesh_base const& geometry,
		float3 const& origin,
		float3 const& direction,
		float const max_distance,
		triangles_predicate_type const& predicate
	) :
	super					( geometry, origin, direction, max_distance, predicate )
{
	if ( math::abs( m_direction.y ) == 1.f )
		query				(
			m_geometry.root( ),
			geometry::vertical_predicate< true >( )
		);
	else
		query				(
			m_geometry.root( ),
			geometry::vertical_predicate< false >( )
		);
}

void ray_test_geometry::test_primitive	(u32 const& triangle)
{
	float					range;
	if ( !test_triangle( triangle, range ) )
		return;

	ASSERT					( !m_result );
	m_result				= (m_predicate)( ray_triangle_result(0, triangle, range) );
}

template < typename T >
void ray_test_geometry::query				( Opcode::AABBNoLeafNode const* const node, T const& predicate )
{
	NORTHSTARCPP_ALIGN(16) float	range;
	if ( !predicate( *this, node, range ) )
		return;

	if ( range > m_max_distance )
		return;

	if ( node->HasPosLeaf( ) )
		test_primitive		( node->GetPosPrimitive( ) );
	else
		query				( node->GetPos( ), predicate );

	if ( m_result )
		return;

	if ( node->HasNegLeaf( ) )
		test_primitive		( node->GetNegPrimitive( ) );
	else
		query				( node->GetNeg( ), predicate );
}