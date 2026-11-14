#ifndef NORTHSTARCPP_CORE_MATH_STANDALONE_H_INCLUDED
#define NORTHSTARCPP_CORE_MATH_STANDALONE_H_INCLUDED

#include <cmath>
#include <cstdlib>
#include <northstarcpp/compat/macro_platform.h>
#include <northstarcpp/compat/macro_building.h>
#include <northstarcpp/compat/type_extensions.h>

#define ASSERT(expression, ...) do { if (!(expression)) std::abort(); } while (false)
#define R_ASSERT(expression, ...) ASSERT(expression)
#define NODEFAULT(...) __VA_ARGS__

#include <northstarcpp/compat/math_extensions.h>

#ifndef NORTHSTARCPP_STANDALONE_MATH
#undef NODEFAULT
#undef R_ASSERT
#undef ASSERT
#endif

#endif
