////////////////////////////////////////////////////////////////////////////
//	Created		: 25.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/tasks_aware_functions.h>

namespace northstarcpp {
namespace tasks {

void	on_current_thread_locks					();
void	on_current_thread_unlocks				();

void   yield (u32 const ms)
{
	if ( ms != 0 )
		tasks::on_current_thread_locks			();
	threading::yield							(ms);
	if ( ms != 0 )
		tasks::on_current_thread_unlocks		();
}

} // namespace tasks
} // namespace northstarcpp
