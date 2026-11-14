////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_DEBUG_CHECK_NO_MULTITHREAD
#define NORTHSTARCPP_DEBUG_CHECK_NO_MULTITHREAD

// instructions:
// inherit from class no_multithread
// put NORTHSTARCPP_CHECK_NO_MULTITHREAD in class functions

#ifdef MASTER_GOLD
#	define NORTHSTARCPP_CHECK_NO_MULTITHREAD
#else // #ifdef MASTER_GOLD
#	define NORTHSTARCPP_CHECK_NO_MULTITHREAD	northstarcpp::debug::detail::check_no_multithread_raii	\
																check_no_multithread_raii$(this)
#endif // #ifdef MASTER_GOLD

namespace northstarcpp {
namespace debug {
namespace detail {
class check_no_multithread_raii;
} // namespace detail
} // namespace debug

class no_multithread
{
public:
			no_multithread	();

private:
	void 	on_function_start	() const;
	void 	on_function_end		() const;

	friend	class				debug::detail::check_no_multithread_raii;

private:
	mutable threading::atomic32_type	m_current_user_thread_id;
	mutable threading::atomic32_type	m_current_num_uses;
};

extern command_line::key		g_dont_check_multithreaded_safety;

} // namespace northstarcpp

#include <northstarcpp/compat/debug_check_no_multithread_inline.h>

#endif // #ifndef NORTHSTARCPP_DEBUG_CHECK_NO_MULTITHREAD