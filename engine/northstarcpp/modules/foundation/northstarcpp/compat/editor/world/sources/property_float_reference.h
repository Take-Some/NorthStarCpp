////////////////////////////////////////////////////////////////////////////
//	Created 	: 17.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_FLOAT_REFERENCE_HPP_INCLUDED
#define PROPERTY_FLOAT_REFERENCE_HPP_INCLUDED

#include "property_holder_include.h"

public ref class property_float_reference :
	public northstarcpp::editor::controls::property_value,
	public northstarcpp::editor::controls::property_incrementable
{
public:
							property_float_reference	(float& value, float const% increment_factor);
	virtual					~property_float_reference	();
							!property_float_reference	();
	virtual System::Object	^get_value					();
	virtual void			set_value					(System::Object ^object);
	virtual void			increment					(float const% increment);

private:
	value_holder<float>*	m_value;
	float					m_increment_factor;
}; // ref class property_float_reference

#endif // ifndef PROPERTY_FLOAT_REFERENCE_HPP_INCLUDED