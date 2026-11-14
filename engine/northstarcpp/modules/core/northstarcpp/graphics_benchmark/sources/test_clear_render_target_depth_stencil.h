////////////////////////////////////////////////////////////////////////////
//	Created		: 15.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_CLEAR_RENDER_TARGET_DEPTH_STENCIL_H_INCLUDED
#define TEST_CLEAR_RENDER_TARGET_DEPTH_STENCIL_H_INCLUDED

#include "benchmark_test.h"

namespace northstarcpp {
namespace graphics_benchmark {

////////////////////////////////////////////////////////////////////////////
class test_clear_depth_stencil: public benchmark_test, public creator_base<test_clear_depth_stencil>
{
public:
	virtual void execute	(benchmark&, u32, u32, gpu_timer&);
}; // class test_clear_depth_stencil


////////////////////////////////////////////////////////////////////////////
class test_clear_render_target: public benchmark_test, public creator_base<test_clear_render_target>
{
public:
	virtual void execute	(benchmark&, u32, u32, gpu_timer&);
}; // class test_clear_render_target

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef TEST_CLEAR_RENDER_TARGET_DEPTH_STENCIL_H_INCLUDED