////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MATH_FRUSTUM_H_INCLUDED
#define NORTHSTARCPP_MATH_FRUSTUM_H_INCLUDED

#include <northstarcpp/compat/math_aabb_plane.h>
#include <northstarcpp/compat/math_cuboid.h>

namespace northstarcpp {
namespace math {

enum class clip_depth { minus_one_to_one, zero_to_one };

class NORTHSTARCPP_CORE_API frustum : public cuboid {
public:
	inline				frustum		( ) { }
						frustum		( float4x4 const& transform );
						frustum		( float4x4 const& transform, clip_depth depth );
}; // class frustum

} // namespace math
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MATH_FRUSTUM_H_INCLUDED