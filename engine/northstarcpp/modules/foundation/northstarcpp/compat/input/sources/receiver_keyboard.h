////////////////////////////////////////////////////////////////////////////
//	Created 	: 21.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RECEIVER_KEYBOARD_H_INCLUDED
#define RECEIVER_KEYBOARD_H_INCLUDED

#include <northstarcpp/compat/input/world.h>
#include <northstarcpp/compat/input/keyboard.h>

struct IDirectInput8A;
struct IDirectInputDevice8A;

namespace northstarcpp {
namespace input {

struct handler;

namespace receiver {

class keyboard :
	public input::keyboard,
	private boost::noncopyable
{
typedef vector< handler* >		handlers_type;
public:
					keyboard		( IDirectInput8A& direct_input, HWND window_handle, northstarcpp::input::world& input_world );
	virtual			~keyboard		( );
	virtual	bool	is_key_down		( enum_keyboard key ) const;
			void	on_activate		( );
			void	on_deactivate	( );
			void	execute			( );
			void	process			( handlers_type& handlers );

private:
			bool	is_key_down		( char value ) const;

private:
	string256				m_current_key_buffer;
	string256				m_previous_key_buffer;
	HWND					m_window_handle;
	IDirectInputDevice8A*	m_device;
	northstarcpp::input::world&		m_world;
}; // class world

} // namespace receiver
} // namespace input
} // namespace northstarcpp

#endif // #ifndef RECEIVER_KEYBOARD_H_INCLUDED