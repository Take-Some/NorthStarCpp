////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_OS_FUNCTIONS_H_INCLUDED
#define NORTHSTARCPP_OS_FUNCTIONS_H_INCLUDED

#include <northstarcpp/macro_building.h>
#include <northstarcpp/type_extensions.h>

namespace northstarcpp {

namespace memory {
	class base_allocator;
} // namespace memory

namespace os {

NORTHSTARCPP_CORE_API void copy_to_clipboard	( pcstr buffer );
NORTHSTARCPP_CORE_API pstr paste_from_clipboard	( northstarcpp::memory::base_allocator& allocator );
NORTHSTARCPP_CORE_API void update_clipboard		( pcstr const string );
NORTHSTARCPP_CORE_API bool is_clipboard_empty	( );

} // namespace os
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_OS_FUNCTIONS_H_INCLUDED