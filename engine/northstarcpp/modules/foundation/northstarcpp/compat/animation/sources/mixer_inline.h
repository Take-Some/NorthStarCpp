////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXER_INLINE_H_INCLUDED
#define MIXER_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {

inline mixer::mixer				( bone_mixer& bone_mixer, memory::base_allocator& allocator ) :
	m_allocator				( allocator ),
	m_last_actual_time_ms	( u32(-1) ),
	m_bone_mixer			( bone_mixer )
{
}

inline void mixer::make_inactual( )
{
	m_last_actual_time_ms	= u32( -1 );
}

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MIXER_INLINE_H_INCLUDED