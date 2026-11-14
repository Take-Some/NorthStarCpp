////////////////////////////////////////////////////////////////////////////
//	Created		: 15.06.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TESTING_IMPL_H_INCLUDED
#define TESTING_IMPL_H_INCLUDED

namespace northstarcpp {
namespace testing {

extern command_line::key	g_run_tests;
extern command_line::key	g_run_tests_and_exit;

NORTHSTARCPP_CORE_API bool	run_tests_command_line	();
NORTHSTARCPP_CORE_API bool	is_testing				();
NORTHSTARCPP_CORE_API void	on_exception			(assert_enum			assert_type, 
											 pcstr					description, 
											 _EXCEPTION_POINTERS*	exception_information,
											 bool					is_assertion);

} // namespace testing
} // namespace northstarcpp

#endif // #ifndef TESTING_IMPL_H_INCLUDED