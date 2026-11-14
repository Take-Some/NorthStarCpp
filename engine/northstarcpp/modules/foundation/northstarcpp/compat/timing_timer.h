////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_TIMING_TIMER_H_INCLUDED
#define NORTHSTARCPP_TIMING_TIMER_H_INCLUDED

namespace northstarcpp {
namespace timing {

class timer {
private:
	u64				m_start_time;

public:
	inline			timer				();
	inline	void	start				();
	inline	u64		get_elapsed_ticks	() const;
	inline	u32		get_elapsed_ms		() const;
	inline	float	get_elapsed_sec		() const;
}; // class timer

} // namespace timing
} // namespace northstarcpp

#include <northstarcpp/compat/timing_timer_inline.h>

#endif // #ifndef NORTHSTARCPP_TIMING_TIMER_H_INCLUDED