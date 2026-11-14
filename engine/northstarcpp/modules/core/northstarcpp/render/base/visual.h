////////////////////////////////////////////////////////////////////////////
//	Created		: 10.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_BASE_VISUAL_H_INCLUDED
#define NORTHSTARCPP_RENDER_BASE_VISUAL_H_INCLUDED

namespace northstarcpp {

namespace collision {
	struct geometry;
} // namespace collision

namespace render {

class visual : public resources::unmanaged_resource {
public:
	virtual	void				render			( )	= 0;
	virtual	void				render_selected	( ) = 0;
	virtual	collision::geometry const* get_collision_geometry	( )	= 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( visual )
}; // class visual

typedef	resources::resource_ptr<
			visual,
			resources::unmanaged_intrusive_base
		> visual_ptr;

} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_BASE_VISUAL_H_INCLUDED