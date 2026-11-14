////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef WORLD_INLINE_H_INCLUDED
#define WORLD_INLINE_H_INCLUDED

namespace northstarcpp {
namespace render {
namespace dx9 {


inline pcstr platform::type				( )
{
	return	( "dx9" );
}

inline u32 platform::frame_id				( )
{
	return	( m_render_frame_id );
}

inline IDirect3DDevice9& platform::device	( ) const
{
	//ASSERT	( m_device );
	return	*hw_wrapper::get_ref().device();//( *m_device );
}

} // namespace dx9
} // namespace render 
} // namespace northstarcpp 


#endif // #ifndef WORLD_INLINE_H_INCLUDED