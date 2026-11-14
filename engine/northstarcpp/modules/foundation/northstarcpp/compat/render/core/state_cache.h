////////////////////////////////////////////////////////////////////////////
//	Created		: 29.03.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef STATE_CACHE_H_INCLUDED
#define STATE_CACHE_H_INCLUDED

namespace northstarcpp {
namespace render_dx10 {
		

template <class IDeviceState, class state_desc>
class state_cache
{
public:
	state_cache();
	~state_cache();

	void			clear_state_array();

	IDeviceState*	get_state( state_desc const & desc );

private:
	struct state_record 
	{
		u32						crc;
		IDeviceState*			state;
	};

private:
	void			create_state	( state_desc desc, IDeviceState** ppIState );
	IDeviceState*	find			( state_desc const & desc, u32 CRC );

private:
	//	This must be cleared on device destroy
	render::vector<state_record>	states;
};

} // namespace render
} // namespace northstarcpp

#include "state_cache_inline.h"

#endif // #ifndef STATE_CACHE_H_INCLUDED
