////////////////////////////////////////////////////////////////////////////
//	Created		: 25.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TASKS_AWARE_MUTEX_H_INCLUDED
#define TASKS_AWARE_MUTEX_H_INCLUDED

namespace northstarcpp {
namespace tasks {

class smart_mutex : private threading::mutex
{
public:
	void	lock	();
	void	unlock	();
	bool	try_lock() { return mutex::try_lock(); }
};

} // namespace tasks
} // namespace northstarcpp


#endif // #ifndef TASKS_AWARE_MUTEX_H_INCLUDED