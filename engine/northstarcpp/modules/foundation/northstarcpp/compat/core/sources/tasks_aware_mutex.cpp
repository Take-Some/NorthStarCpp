////////////////////////////////////////////////////////////////////////////
//	Created		: 25.11.2009
//	Author		: 
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/tasks_aware_mutex.h>

namespace northstarcpp {
namespace tasks {

u32		spin_count_before_notify_task_system	();
void	on_current_thread_locks					();
void	on_current_thread_unlocks				();

void   smart_mutex::lock ()
{
	for ( u32 i=0; i<spin_count_before_notify_task_system(); ++i )
		if ( mutex::try_lock() )
			return;

	tasks::on_current_thread_locks				();
	mutex::lock									();
	tasks::on_current_thread_unlocks			();
}

void   smart_mutex::unlock ()
{
	mutex::unlock								();
}

} // namespace tasks
} // namespace northstarcpp
