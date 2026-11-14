////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_property_container.h"
#include "property_holder.h"

using System::Object;

property_property_container::property_property_container(northstarcpp::editor::property_holder_ptr object) 
{
	m_object = NEW(northstarcpp::editor::property_holder_ptr)(object);
}

Object^ property_property_container::get_value			()
{
	::property_holder*			real_value = dynamic_cast<::property_holder*>(&**m_object);

	return		((real_value)->container());
}

void property_property_container::set_value				(Object ^object)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER		( object );
	UNREACHABLE_CODE();
}