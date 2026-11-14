////////////////////////////////////////////////////////////////////////////
//	Created		: 22.06.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CORE_TEST_SUITE_H_INCLUDED
#define NORTHSTARCPP_CORE_TEST_SUITE_H_INCLUDED

#include <northstarcpp/testing.h>
#include <northstarcpp/linkage_helper.h>

INCLUDE_TO_LINKAGE(resources_test);
INCLUDE_TO_LINKAGE(compressor_test);
INCLUDE_TO_LINKAGE(threading_test);
INCLUDE_TO_LINKAGE(math_tests);

namespace northstarcpp {

class NORTHSTARCPP_CORE_API core_test_suite : public testing::suite_base<core_test_suite>
{
public:
	DEFINE_SUITE_HELPERS

public:
	void	set_resource_path	(pcstr resource_path)	{ m_resource_path = resource_path; }
	pcstr	get_resource_path	() const				{ return m_resource_path.c_str(); }

private:
	fs::path_string		m_resource_path;
}; // class core_test_suite

} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CORE_TEST_SUITE_H_INCLUDED