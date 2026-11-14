////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_EDITOR_WORLD_H_INCLUDED
#define NORTHSTARCPP_EDITOR_WORLD_H_INCLUDED

namespace northstarcpp {
namespace editor {

class property_holder;
class property_holder_collection;
class property_holder_holder;
struct NORTHSTARCPP_NOVTABLE engine;

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void	load			( ) = 0;
	virtual	void	run				( ) = 0;
	virtual	void	clear_resources	( ) = 0;
	virtual	HWND	main_handle		( ) = 0;
	virtual	HWND	view_handle		( ) = 0;
	virtual	void	editor_mode		( bool beditor_mode) = 0;
	virtual	void	render			( u32 frame_id, math::float4x4 view_matrix ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_EDITOR_WORLD_H_INCLUDED