////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_NETWORK_WORLD_H_INCLUDED
#define NORTHSTARCPP_NETWORK_WORLD_H_INCLUDED

namespace northstarcpp {
namespace network {

struct handler;

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void	tick			( u32 const logic_frame_id ) = 0;
	virtual	void	clear_resources	( ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace network
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_NETWORK_WORLD_H_INCLUDED