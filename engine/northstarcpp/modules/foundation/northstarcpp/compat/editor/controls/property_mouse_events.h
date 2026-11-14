////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_MOUSE_EVENTS_H_INCLUDED
#define PROPERTY_MOUSE_EVENTS_H_INCLUDED

namespace northstarcpp {
namespace editor {
namespace controls {

ref class property_grid;

public interface class property_mouse_events {
public:
	virtual void	on_double_click	(property_grid^ property_grid) = 0;
}; // interface class property_mouse_events

} // namespace controls
} // namespace editor
} // namespace northstarcpp

#endif // ifndef PROPERTY_MOUSE_EVENTS_H_INCLUDED