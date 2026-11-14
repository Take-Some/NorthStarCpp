////////////////////////////////////////////////////////////////////////////
//	Created 	: 23.01.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_CONTAINER_INTERFACE_H_INCLUDED
#define PROPERTY_CONTAINER_INTERFACE_H_INCLUDED

namespace northstarcpp {
namespace editor {
namespace controls {

interface class property_value;

public interface class property_container_interface {
public:
	typedef Flobbster::Windows::Forms::PropertySpec	PropertySpec;

public:
	virtual	property_value^	value	(PropertySpec^ description) = 0;
}; // interface class property_container_interface

} // namespace controls
} // namespace editor
} // namespace northstarcpp

#endif // ifndef PROPERTY_CONTAINER_INTERFACE_H_INCLUDED