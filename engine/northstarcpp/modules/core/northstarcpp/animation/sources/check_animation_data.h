////////////////////////////////////////////////////////////////////////////
//	Created		: 08.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CHECK_ANIMATION_DATA_H_INCLUDED
#define CHECK_ANIMATION_DATA_H_INCLUDED

namespace northstarcpp {

namespace configs{
	class lua_config_value;
} // namespace configs

namespace animation {


void	test_data(	configs::lua_config_value	const &animation_data, 
					configs::lua_config_value	const &check_data,
					world &world );

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef CHECK_ANIMATION_DATA_H_INCLUDED