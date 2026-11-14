////////////////////////////////////////////////////////////////////////////
//	Created		: 15.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "test_simple_shader_op.h"
#include "cpu_timer.h"
#include "gpu_timer.h"
#include "benchmark.h"
#include "northstarcpp/render/core/effect_descriptor.h"
#include "test_clear_render_target_depth_stencil.h"
#include "northstarcpp/math_randoms_generator.h"

namespace northstarcpp {
namespace graphics_benchmark {


void test_clear_depth_stencil::execute	(benchmark&, u32 current_pass, u32 num_passes, gpu_timer&)
{
	northstarcpp::math::random32 r;
	r.seed(100);
	northstarcpp::render_dx10::backend::ref().clear_depth_stencil( D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, (float)current_pass/(float)num_passes, static_cast_checked<u8>(r.random(255)));
}

void test_clear_render_target::execute	(benchmark&, u32 current_pass, u32 num_passes, gpu_timer&)
{
	float val = (float)current_pass/(float)num_passes;
	northstarcpp::render_dx10::backend::ref().clear_render_targets( northstarcpp::math::color( val, val, val));
}

} // namespace graphics_benchmark
} // namespace northstarcpp