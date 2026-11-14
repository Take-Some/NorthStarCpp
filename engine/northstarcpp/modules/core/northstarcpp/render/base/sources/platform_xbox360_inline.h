////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef WORLD_INLINE_H_INCLUDED
#define WORLD_INLINE_H_INCLUDED

namespace northstarcpp {
namespace render {
namespace xbox360 {

inline renderer& platform::extended( )
{
	return	( *this );
}

inline pcstr platform::type		( )
{
	return	( "xbox360" );
}

inline D3DDevice& platform::device	( ) const
{
	ASSERT	( m_device );
	return	( *m_device );
}

inline u32 platform::frame_id		( )
{
	return	( m_frame_id );
}

} // namespace xbox360
} // namespace render
} // namespace northstarcpp

#endif // #ifndef WORLD_INLINE_H_INCLUDED