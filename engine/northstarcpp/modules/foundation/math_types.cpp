// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/compat/core/math_standalone.h>

#include <northstarcpp/compat/math_float3_inline.h>
#include <northstarcpp/compat/math_float4_inline.h>
#include <northstarcpp/compat/math_float4x4_inline.h>
#include <northstarcpp/compat/math_sphere_inline.h>
#include <northstarcpp/compat/math_aabb_inline.h>
#include <northstarcpp/compat/math_quaternion_inline.h>

namespace northstarcpp::math {

bool similar(float left, float right, float epsilon)
{
    return abs(left - right) < epsilon;
}

bool is_zero(float value, float epsilon)
{
    return similar(value, 0.f, epsilon);
}

} // namespace northstarcpp::math
