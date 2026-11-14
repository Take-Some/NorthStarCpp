////////////////////////////////////////////////////////////////////////////
//	Created		: 07.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/fs_path.h>

namespace northstarcpp {
namespace fs {

path_string   convert_to_native (pcstr const path)
{
	return path_string(path);
}

} // namespace fs
} // namespace northstarcpp