////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_string_values_value_shared_str.h"

property_string_values_value_shared_str::property_string_values_value_shared_str	(
		northstarcpp::editor::engine* engine,
		northstarcpp::shared_string& value,
		LPCSTR const* values,
		u32 const &value_count
	) :
	inherited				(engine, value),
	m_collection			(gcnew collection_type())
{
	for (u32 i=0; i<value_count; ++i)
		m_collection->Enqueue	(gcnew System::String(values[i]));
}
