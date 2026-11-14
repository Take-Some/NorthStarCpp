////////////////////////////////////////////////////////////////////////////
//	Created		: 26.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef DIALOG_EDITOR_MEMORY_H_INCLUDED
#define DIALOG_EDITOR_MEMORY_H_INCLUDED

#include <northstarcpp/buffer_vector.h>
#include <northstarcpp/fixed_vector.h>
#include <northstarcpp/associative_vector.h>
#include <northstarcpp/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

#include <northstarcpp/editor/dialog/api.h>

namespace northstarcpp {
namespace dialog_editor {

extern allocator_type*							g_allocator;

#define USER_ALLOCATOR							*::northstarcpp::dialog_editor::g_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

} // namespace dialog_editor
} // namespace northstarcpp

#define NEW( type )								NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::dialog_editor::g_allocator, type )
#define DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::dialog_editor::g_allocator, pointer )
#define MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::dialog_editor::g_allocator, size, description )
#define REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::dialog_editor::g_allocator, pointer, size, description )
#define FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::dialog_editor::g_allocator, pointer )
#define ALLOC( type, count )					NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::dialog_editor::g_allocator, type, count )

#endif // #ifndef DIALOG_EDITOR_MEMORY_H_INCLUDED