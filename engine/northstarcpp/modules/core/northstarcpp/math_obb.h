////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_OBB_H_INCLUDED
#define NORTHSTARCPP_MATH_OBB_H_INCLUDED

namespace northstarcpp {
namespace math {

class NORTHSTARCPP_CORE_API obb : public float4x4 {
public:
			bool	intersect	( float3 const& position, float3 const& direction, type& distance );
}; // class obb

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_OBB_H_INCLUDED