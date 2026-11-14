////////////////////////////////////////////////////////////////////////////
//	Created 	: 09.01.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_INTEGER_VALUES_VALUE_REFERENCE_GETTER_HPP_INCLUDED
#define PROPERTY_INTEGER_VALUES_VALUE_REFERENCE_GETTER_HPP_INCLUDED

#include "property_integer_reference.h"
#include "property_integer_values_value_base.h"

public ref class property_integer_values_value_reference_getter :
	public property_integer_reference,
	public property_integer_values_value_base
{
public:
	typedef northstarcpp::editor::property_holder									property_holder;
private:
	typedef property_integer_reference								inherited;
	typedef System::Object											Object;
	typedef System::Collections::IList								IList;

public:
							property_integer_values_value_reference_getter	(
								int& value,
								string_collection_getter_type^ collection_getter,
								string_collection_size_getter_type^ collection_size_getter
							);
	virtual Object			^get_value										() override;
	virtual void			set_value										(Object ^object) override;
	virtual	IList^			collection										();

public:
	string_collection_getter_type^		m_collection_getter;
	string_collection_size_getter_type^	m_collection_size_getter;
}; // ref class property_integer_values_value_getter

#endif // ifndef PROPERTY_INTEGER_VALUES_VALUE_REFERENCE_GETTER_HPP_INCLUDED