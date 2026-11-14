////////////////////////////////////////////////////////////////////////////
//	Created		: 02.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef QUATERNION_MIX_H_INCLUDED
#define QUATERNION_MIX_H_INCLUDED

#include "animation_layer.h"

namespace northstarcpp {
namespace animation {

class animation_layer;

float4x4 qslim_mix		( const animation_vector_type &blends, u32 bone, float time );
float4x4 sasquatch_mix	( const animation_vector_type &blends, u32 bone, float time );
float4x4 multilinear_mix( const animation_vector_type &blends, u32 bone, float time );

}  // namespace animation
}  // namespace northstarcpp

#endif // #ifndef QSLIM_MIX_H_INCLUDED