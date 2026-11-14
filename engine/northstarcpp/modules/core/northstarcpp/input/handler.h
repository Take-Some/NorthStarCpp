////////////////////////////////////////////////////////////////////////////
//	Created 	: 21.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INPUT_HANDLER_H_INCLUDED
#define NORTHSTARCPP_INPUT_HANDLER_H_INCLUDED

#ifdef __SNC__
#	include <northstarcpp/input/gamepad.h>
#	include <northstarcpp/input/mouse.h>
#	include <northstarcpp/input/keyboard.h>
#endif // #ifdef __SNC__

namespace northstarcpp {
namespace input {

struct world;

struct gamepad;
struct keyboard;
struct mouse;

enum gamepad_button;
enum enum_gamepad_action;

enum enum_keyboard;
enum enum_keyboard_action;

enum mouse_button;
enum enum_mouse_key_action;

struct NORTHSTARCPP_NOVTABLE handler {
public:
	virtual	bool	on_keyboard_action	( world& world, enum_keyboard key, enum_keyboard_action action )	= 0;
	virtual	bool	on_gamepad_action	( world& world, gamepad_button button, enum_gamepad_action action )	= 0;
	virtual	bool	on_mouse_key_action	( world& world, mouse_button button, enum_mouse_key_action action )	= 0;
	virtual	bool	on_mouse_move		( world& world, int x, int y, int z )								= 0;
	virtual int		input_priority		( ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( handler )
}; // class handler

} // namespace input
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_INPUT_HANDLER_H_INCLUDED