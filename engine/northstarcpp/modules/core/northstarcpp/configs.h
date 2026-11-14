////////////////////////////////////////////////////////////////////////////
//	Created		: 09.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CONFIGS_H_INCLUDED
#define NORTHSTARCPP_CONFIGS_H_INCLUDED

namespace northstarcpp {
namespace configs {

enum enum_types {
	t_boolean,
	t_integer,
	t_float,
	t_table_named,
	t_table_indexed,
	t_embedded_types		= t_table_indexed,
	t_string,
	t_float2,
	t_float3,
	t_float4,
	t_max					= u16(-1)
}; // enum enum_types

} // namespace configs
} // namespace northstarcpp

#include <northstarcpp/configs_binary_config.h>
#include <northstarcpp/configs_lua_config.h>

#endif // #ifndef NORTHSTARCPP_CONFIGS_H_INCLUDED