////////////////////////////////////////////////////////////////////////////
//	Created		: 28.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/resources_name_registry_entry.h>
#include "fs_helper.h"

namespace northstarcpp {
namespace resources {

namespace detail {

u32   name_registry_hash::operator () (name_registry_entry & entry) const
{
	return	northstarcpp::fs::crc32(entry.name, strings::length(entry.name));
}

bool   name_registry_equal::operator () (name_registry_entry const & left, name_registry_entry const & right) const
{
	return	strings::equal(left.name, right.name);
}

} // namespace detail

} // namespace resources
} // namespace northstarcpp
