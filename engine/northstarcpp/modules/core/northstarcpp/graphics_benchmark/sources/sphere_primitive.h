////////////////////////////////////////////////////////////////////////////
//	Created		: 18.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SPHERE_PRIMITIVE_H_INCLUDED
#define SPHERE_PRIMITIVE_H_INCLUDED

#include "benchmark_test.h"

namespace northstarcpp {
namespace graphics_benchmark {

#define SPHERE_NUMVERTEX 92
#define SPHERE_NUMFACES 180
#define SPHERE_NUMVERTEXL 60
#define SPHERE_NUMLINES 60

extern  float3	sphere_vertices[];
extern  WORD	sphere_faces[];
extern  float3	sphere_verticesl[];
extern  WORD	sphere_lines[];


} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef SPHERE_PRIMITIVE_H_INCLUDED
