////////////////////////////////////////////////////////////////////////////
//	Created		: 10.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CONFIG_LTX_H_INCLUDED
#define CONFIG_LTX_H_INCLUDED

#include <northstarcpp/configs.h>

namespace northstarcpp {
namespace core {
namespace configs {

class binary_config;

void			parse_string_data	( memory::reader F, memory::base_allocator* allocator, pbyte* dest, u32& dest_size );
binary_config*	create_from_ltx_text( pcstr text, memory::base_allocator* allocator, pcstr file_name );

} // namespace configs
} // namespace core
} // namespace northstarcpp

#endif // #ifndef CONFIG_LTX_H_INCLUDED