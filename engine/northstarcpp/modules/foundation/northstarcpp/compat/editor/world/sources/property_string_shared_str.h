////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PROPERTY_STRING_SHARED_STR_HPP_INCLUDED
#define PROPERTY_STRING_SHARED_STR_HPP_INCLUDED

#include "property_holder_include.h"

namespace editor {
	class engine;
} // namespace editor

public ref class property_string_shared_str : public northstarcpp::editor::controls::property_value {
public:
							property_string_shared_str	(northstarcpp::editor::engine* engine, northstarcpp::shared_string& value);
	virtual					~property_string_shared_str	();
							!property_string_shared_str	();
	virtual System::Object	^get_value					();
	virtual void			set_value					(System::Object ^object);

private:
	northstarcpp::editor::engine*			m_engine;
	northstarcpp::shared_string*	m_value;
}; // ref class property_string_shared_str

#endif // ifndef PROPERTY_STRING_SHARED_STR_HPP_INCLUDED