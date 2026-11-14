////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_string_shared_str.h"
#include <northstarcpp/compat/editor/world/engine.h>

property_string_shared_str::property_string_shared_str			( northstarcpp::editor::engine* engine, northstarcpp::shared_string& value) :
	m_engine				(engine),
	m_value					(&value)
{
}

property_string_shared_str::~property_string_shared_str			()
{
	this->!property_string_shared_str	();
}

property_string_shared_str::!property_string_shared_str			()
{
}

System::Object ^property_string_shared_str::get_value	()
{
//	return					( gcnew System::String(m_engine->value(*m_value)));
	return					( gcnew System::String(m_value->c_str()));
}

void property_string_shared_str::set_value			(System::Object ^object)
{
//	m_engine->value			( northstarcpp::editor::unmanaged_string( safe_cast< System::String^ >( object ) ).c_str( ), *m_value);
	*m_value				= northstarcpp::editor::unmanaged_string( safe_cast< System::String^ >( object ) ).c_str( );
}
