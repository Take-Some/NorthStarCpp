////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCES_IMPL_H_INCLUDED
#define RESOURCES_IMPL_H_INCLUDED

#include "resources_helper.h"

#include <northstarcpp/resources_resource.h>
#include <northstarcpp/resources.h>

namespace northstarcpp {
namespace resources {

void				initialize			();
void				mount_mounts_path	(pcstr mounts_path);
bool				is_initialized		();
void				finalize			();

} // namespace northstarcpp
} // namespace resources

#endif // #ifndef RESOURCES_IMPL_H_INCLUDED