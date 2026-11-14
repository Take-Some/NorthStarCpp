////////////////////////////////////////////////////////////////////////////
//	Created		: 13.03.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_DEBUG_H_INCLUDED
#define MEMORY_DEBUG_H_INCLUDED

#include <northstarcpp/compat/platform_extensions.h>

namespace northstarcpp {
namespace memory {

template <typename T>
inline T	uninitialized_value						( )
{
	return horrible_cast<u32,T>( ::northstarcpp::platform::little_endian() ? 0xfdfdcdcd : 0xcdcdfdfd ).second;
}

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef MEMORY_DEBUG_H_INCLUDED