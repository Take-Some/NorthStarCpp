////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_string_values_value.h"

property_string_values_value::property_string_values_value	(
		string_getter_type^ getter,
		string_setter_type^ setter,
		System::Collections::ArrayList^ values
	) :
	inherited				(getter, setter),
	m_collection			(gcnew collection_type())
{
	for each (System::String^ s in values)
		m_collection->Enqueue	(s);
}
