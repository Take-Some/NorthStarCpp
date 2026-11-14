#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <northstarcpp/compat/buffer_vector.h>
#include <northstarcpp/compat/fixed_vector.h>
#include <northstarcpp/compat/associative_vector.h>
#include <northstarcpp/compat/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

#include <northstarcpp/compat/ui/api.h>

namespace northstarcpp {
namespace ui {

extern allocator_type*							g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::ui::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

} // namespace ui
} // namespace northstarcpp

#define NEW(type)							NORTHSTARCPP_NEW_IMPL		(	*::northstarcpp::ui::g_allocator, type)
#define DELETE(pointer)						NORTHSTARCPP_DELETE_IMPL	(	*::northstarcpp::ui::g_allocator, pointer)
#define MALLOC(size, description)			NORTHSTARCPP_MALLOC_IMPL	(	*::northstarcpp::ui::g_allocator, size, description)
#define REALLOC(pointer, size, description)	NORTHSTARCPP_REALLOC_IMPL	(	*::northstarcpp::ui::g_allocator, pointer, size, description)
#define FREE(pointer)						NORTHSTARCPP_FREE_IMPL		(	*::northstarcpp::ui::g_allocator, pointer)
#define ALLOC(type, count)					NORTHSTARCPP_ALLOC_IMPL		(	*::northstarcpp::ui::g_allocator, type, count)

#endif // #ifndef MEMORY_H_INCLUDED