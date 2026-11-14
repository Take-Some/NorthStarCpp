////////////////////////////////////////////////////////////////////////////
//	Created		: 29.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SOUND_WORLD_INLINE_H_INCLUDED
#define SOUND_WORLD_INLINE_H_INCLUDED

namespace northstarcpp {
namespace sound {

template <typename T> 
inline void sound_world::delete_orders( T& queue )
{
	queue.set_pop_thread_id();
	while ( !queue.empty( ) ) 
	{
		sound_command*			to_delete;
		queue.pop_front			( to_delete );
		NORTHSTARCPP_DELETE_IMPL		( to_delete->m_creator, to_delete );
	}
}

} // namespace sound
} // namespace northstarcpp

#endif // #ifndef SOUND_WORLD_INLINE_H_INCLUDED