////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCES_HELPER_H_INCLUDED
#define RESOURCES_HELPER_H_INCLUDED

#include "fs_file_system.h"

namespace northstarcpp {
namespace resources {
	typedef	fs::file_system::iterator						fat_iterator;
} // namespace resources
} // namespace northstarcpp

#include "resources_macros.h"
#include <northstarcpp/resources.h>
#include <northstarcpp/resources_fs.h>
#include <northstarcpp/fs_path.h>

namespace northstarcpp {
namespace resources {

#define USER_ALLOCATOR	::northstarcpp::memory::g_resources_helper_allocator
#include <northstarcpp/std_containers.h>
#undef USER_ALLOCATOR

//----------------------------------------------------------
// general
//----------------------------------------------------------
class				queries_result;

typedef	fastdelegate::FastDelegate< void (bool result) >	store_callback;

fat_iterator		wrapper_to_fat_it 				(fat_it_wrapper wrapper);
fat_it_wrapper		fat_it_to_wrapper 				(fat_iterator it);

class				query_result;
fs::path_string		get_native_file_path			(query_result const* query, bool assert_on_fail = true);

class raw_file_buffer_helper
{
public:
	raw_file_buffer_helper *	next;
	class_id					resource_class;
};

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef RESOURCES_HELPER_H_INCLUDED