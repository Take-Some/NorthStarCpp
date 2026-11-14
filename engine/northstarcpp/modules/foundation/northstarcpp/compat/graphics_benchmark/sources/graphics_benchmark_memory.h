////////////////////////////////////////////////////////////////////////////
//	Created		: 09.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GRAPHICS_BENCHMARK_MEMORY_H_INCLUDED
#define GRAPHICS_BENCHMARK_MEMORY_H_INCLUDED

#include <northstarcpp/compat/buffer_vector.h>
#include <northstarcpp/compat/fixed_vector.h>
#include <northstarcpp/compat/associative_vector.h>
#include <northstarcpp/compat/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

namespace northstarcpp {
namespace graphics_benchmark {

extern	northstarcpp::memory::doug_lea_allocator_type	g_allocator;

} // namespace graphics_benchmark
} // namespace northstarcpp

#include <northstarcpp/compat/render/base/render_allocator.h>

#endif // #ifndef GRAPHICS_BENCHMARK_MEMORY_H_INCLUDED