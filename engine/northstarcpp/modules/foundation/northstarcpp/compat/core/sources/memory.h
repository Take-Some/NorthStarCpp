////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#if defined(_MSC_VER)
#	define NORTHSTARCPP_PRINTF_SPEC_LONG_LONG( digits )			"%" NORTHSTARCPP_MAKE_STRING( digits ) "I64d"
#	define NORTHSTARCPP_PRINTF_SPEC_LONG_LONG_HEX( digits )		"%" NORTHSTARCPP_MAKE_STRING( digits ) "I64x"
#else // #if defined(_MSC_VER)
#	define NORTHSTARCPP_PRINTF_SPEC_LONG_LONG( digits )			"%" NORTHSTARCPP_MAKE_STRING( digits ) "lld"
#	define NORTHSTARCPP_PRINTF_SPEC_LONG_LONG_HEX( digits )		"%" NORTHSTARCPP_MAKE_STRING( digits ) "llx"
#endif // #if defined(_MSC_VER)

namespace northstarcpp {
namespace memory {

void preinitialize		( );
void initialize			( );
void finalize			( );

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef MEMORY_H_INCLUDED