////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_FILE_NAME_VALUE_BASE_HPP_INCLUDED
#define PROPERTY_FILE_NAME_VALUE_BASE_HPP_INCLUDED

interface class property_file_name_value_base : public northstarcpp::editor::controls::property_value {
	virtual	System::String^	default_extension	() = 0;
	virtual	System::String^	filter				() = 0;
	virtual	System::String^	initial_directory	() = 0;
	virtual	System::String^	title				() = 0;
	virtual	bool			remove_extension	() = 0;
}; // interface class property_file_name_value_base

#endif // ifndef PROPERTY_FILE_NAME_VALUE_BASE_HPP_INCLUDED