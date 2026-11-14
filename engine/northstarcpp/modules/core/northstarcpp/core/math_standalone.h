#ifndef NORTHSTARCPP_CORE_MATH_STANDALONE_H_INCLUDED
#define NORTHSTARCPP_CORE_MATH_STANDALONE_H_INCLUDED

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <malloc.h>
#include <northstarcpp/macro_platform.h>
#include <northstarcpp/macro_building.h>
#include <northstarcpp/type_extensions.h>

#define ASSERT(expression, ...) do { if (!(expression)) std::abort(); } while (false)
#define R_ASSERT(expression, ...) ASSERT(expression)
#define R_ASSERT_U(expression, ...) ASSERT(expression)
#define CURE_ASSERT(expression, action, ...) do { if (!(expression)) { action; } } while(false)
#ifndef NORTHSTARCPP_UNREFERENCED_PARAMETER
#define NORTHSTARCPP_UNREFERENCED_PARAMETER(value) (void)(value)
#endif
#ifndef ALLOCA
#define ALLOCA(size) alloca(size)
#endif
#define NODEFAULT(...) __VA_ARGS__

#include <northstarcpp/math_extensions.h>

#ifndef NORTHSTARCPP_STANDALONE_MATH
#undef NODEFAULT
#undef R_ASSERT
#undef ASSERT
#endif

#endif
