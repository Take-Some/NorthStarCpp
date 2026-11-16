////////////////////////////////////////////////////////////////////////////
//	Created		: 18.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_AABB_PLANE_H_INCLUDED
#define NORTHSTARCPP_MATH_AABB_PLANE_H_INCLUDED

namespace northstarcpp {
namespace math {

class NORTHSTARCPP_CORE_API aabb_plane {
public:
	void				normalize	();
	intersection		test		( aabb const& aabb ) const;

public:
	plane				plane;

private:
	u32					m_lut_id;
}; // struct aabb_plane

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_AABB_PLANE_H_INCLUDED