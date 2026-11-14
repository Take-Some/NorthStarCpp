////////////////////////////////////////////////////////////////////////////
//	Created		: 16.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SOUND_ITEM_CALLBACK_H_INCLUDED
#define SOUND_ITEM_CALLBACK_H_INCLUDED

#include <northstarcpp/intrusive_spsc_queue.h>

namespace northstarcpp {
namespace sound {

class sound_world;
class sound_item;

struct callback_command
{
	enum cb_event{ev_unknown, ev_stream_end, ev_buffer_end, ev_buffer_error};

				callback_command		( cb_event const& ev, sound_item* owner, void* context):m_event_type(ev),m_owner(owner),m_context(context){ };
				~callback_command		( )		{};
		void	execute					( );

	callback_command*		m_next;
	cb_event				m_event_type;
	sound_item*				m_owner;
	void*					m_context;
}; // struct callback_command

typedef intrusive_spsc_queue<callback_command, callback_command, &callback_command::m_next>	callback_commands_queue_type;

} // namespace sound
} // namespace northstarcpp

#endif // #ifndef SOUND_ITEM_CALLBACK_H_INCLUDED