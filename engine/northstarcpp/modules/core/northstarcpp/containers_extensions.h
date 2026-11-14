////////////////////////////////////////////////////////////////////////////
//	Created 	: 01.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CONTAINERS_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_CONTAINERS_EXTENSIONS_H_INCLUDED

#include <northstarcpp/c_array_functions.h>

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>

#include <map>
#include <vector>
#include <list>
#include <set>
#include <deque>

namespace northstarcpp {

#include <northstarcpp/vectora.h>

namespace mt {

#define USER_ALLOCATOR		::northstarcpp::memory::g_mt_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

} // namespace mt
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CONTAINERS_EXTENSIONS_H_INCLUDED