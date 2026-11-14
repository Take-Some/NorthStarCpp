////////////////////////////////////////////////////////////////////////////
//	Created		: 10.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TRIANGLE_BOX_CONTACT_TESTS_H_INCLUDED
#define TRIANGLE_BOX_CONTACT_TESTS_H_INCLUDED

#include <northstarcpp/compat/collision/contact_info.h>
#include "triangle_box_res.h"
#include "triangle_mesh_primitive_contact_tests.h"
namespace northstarcpp {
namespace collision {
struct on_contact;
class box_geometry;
namespace detail{

class triangle_box_contact_tests:
	public triangle_mesh_primitive_contact_tests<triangle_box_res>,
	private boost::noncopyable
{
private:
	typedef triangle_mesh_primitive_contact_tests<triangle_box_res>		super;

public:
inline			triangle_box_contact_tests	(	const box_geometry &g, const float4x4 &transform,
												buffer_vector<result>& result_buff, on_contact& c );
inline static	void	get_query_aabb			(  const float4x4 &transform, const box_geometry& og,   math::aabb	&query_aabb );
public:
	typedef box_geometry geometry_type;
public:
			void	test				( buffer_vector<u32> &backs, const u32 * ind, float3 const* verts );
private:
	const box_geometry					&m_geometry;

}; // class triangle_box_contact_tests

}//detail
}//collision
}//northstarcpp

#include "triangle_box_contact_tests_inline.h"

#endif // #ifndef TRIANGLE_BOX_CONTACT_TESTS_H_INCLUDED