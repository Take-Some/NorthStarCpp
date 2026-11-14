////////////////////////////////////////////////////////////////////////////
//	Created 	: 07.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_VALUE_H_INCLUDED
#define PROPERTY_VALUE_H_INCLUDED

namespace northstarcpp {
namespace editor {
namespace controls {

public interface class property_value {
public:
	virtual System::Object	^get_value		() = 0;
	virtual void			set_value		(System::Object ^object) = 0;
}; // interface class property_value

} // namespace controls
} // namespace editor
} // namespace northstarcpp

#endif // ifndef PROPERTY_VALUE_H_INCLUDED