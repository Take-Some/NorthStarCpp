////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NETWORK_WORLD_H_INCLUDED
#define NETWORK_WORLD_H_INCLUDED

#include <northstarcpp/compat/network/world.h>
#include "platform_initializer.h"

namespace northstarcpp {
namespace network {

namespace lowlevel {
	class io_service;
} // namespace lowlevel

struct engine;

class network_world :
	public network::world,
	private boost::noncopyable
{
public:
					network_world	( network::engine& engine );
	virtual			~network_world	( );
	virtual	void	tick			(  u32 const logic_frame_id);
	virtual	void	clear_resources	( );

private:
	engine&							m_engine;
	lowlevel::platform_initializer	m_net_init;
	lowlevel::io_service*			m_ioservice;
}; // class world

} // namespace network
} // namespace northstarcpp

#endif // #ifndef NETWORK_WORLD_H_INCLUDED