////////////////////////////////////////////////////////////////////////////
//	Created 	: 26.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_INCREMENTABLE_H_INCLUDED
#define PROPERTY_INCREMENTABLE_H_INCLUDED

namespace northstarcpp {
namespace editor {
namespace controls {

public interface class property_incrementable {
public:
	virtual void	increment	(float const% increment) = 0;
}; // interface class property_value

} // namespace controls
} // namespace editor
} // namespace northstarcpp

#endif // ifndef PROPERTY_INCREMENTABLE_H_INCLUDED