////////////////////////////////////////////////////////////////////////////
//	Created		: 21.12.2009
//	Author		: Alexander Maniluk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#if !NORTHSTARCPP_PLATFORM_PS3
#include "boost_api.h"

namespace boost
{
#if !NORTHSTARCPP_STATIC_LIBRARIES
	void throw_exception(std::exception const & e)
	{
		FATAL("boost exception caught: %s", e.what());
	}
#endif // #if !NORTHSTARCPP_STATIC_LIBRARIES
	void throw_exception(std::bad_alloc const & e)
	{
		NORTHSTARCPP_UNREFERENCED_PARAMETER	( e );
		FATAL("boost exception caught: %s", e.what());
	}
} // namespace boost
#endif // #if !NORTHSTARCPP_PLATFORM_PS3