////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_SOUND_WORLD_H_INCLUDED
#define NORTHSTARCPP_SOUND_WORLD_H_INCLUDED

namespace northstarcpp {
namespace sound {

struct handler;

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void	tick					( u32 logic_frame_id )		= 0;

	virtual void	set_listener_properties	( float4x4 const& inv_view_matrix, ::northstarcpp::memory::base_allocator* a )	= 0;

	virtual	void	clear_resources			( ) 						= 0;
	virtual	void	clear_logic_resources	( ) 						= 0;
	virtual	void	clear_editor_resources	( ) 						= 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace sound
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_SOUND_WORLD_H_INCLUDED