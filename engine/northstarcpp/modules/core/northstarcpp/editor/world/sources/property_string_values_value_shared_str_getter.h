////////////////////////////////////////////////////////////////////////////
//	Created 	: 09.01.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_STRING_VALUES_VALUE_SHARED_STR_GETTER_HPP_INCLUDED
#define PROPERTY_STRING_VALUES_VALUE_SHARED_STR_GETTER_HPP_INCLUDED

#include "property_string_shared_str.h"
#include "property_string_values_value_base.h"

public ref class property_string_values_value_shared_str_getter :
	public property_string_shared_str,
	public property_string_values_value_base
{
private:
	typedef property_string_shared_str												inherited;

public:
	typedef property_string_values_value_base::collection_type						collection_type;

public:
								property_string_values_value_shared_str_getter	(
									northstarcpp::editor::engine* engine,
									northstarcpp::shared_string& value,
									string_collection_getter_type^ collection_getter,
									string_collection_size_getter_type^ collection_size_getter
								);
	virtual	collection_type^	values											();

public:
	string_collection_getter_type^		m_collection_getter;
	string_collection_size_getter_type^	m_collection_size_getter;
}; // ref class property_string_values_value_shared_str_getter

#endif // ifndef PROPERTY_STRING_VALUES_VALUE_SHARED_STR_GETTER_HPP_INCLUDED