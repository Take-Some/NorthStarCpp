////////////////////////////////////////////////////////////////////////////
//	Created 	: 04.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "window_ide.h"
#include "window_view.h"
#include "project_tab.h"
#include "tools_tab.h"
#include "editor_world.h"

using northstarcpp::editor::window_ide;
using Microsoft::Win32::Registry;

template <typename T>
inline static T registry_value				(RegistryKey ^key, String ^value_id, const T &default_value)
{
	array<String^>^		names = key->GetValueNames();
	if (names->IndexOf(names,value_id) >= 0)
		return			((T)key->GetValue(value_id));

	return				(default_value);
}

RegistryKey^ window_ide::base_registry_key	()
{
	RegistryKey^		software = Registry::CurrentUser->OpenSubKey( "Software", true );
	ASSERT				(software);

	RegistryKey^		company = software->OpenSubKey( NORTHSTARCPP_ENGINE_COMPANY_ID, true );
	if (!company)
		company			= software->CreateSubKey( NORTHSTARCPP_ENGINE_COMPANY_ID );
	ASSERT				(company);
	software->Close		();

	RegistryKey^		product = company->OpenSubKey( NORTHSTARCPP_ENGINE_ID, true );
	if (!product)
		product			= company->CreateSubKey( NORTHSTARCPP_ENGINE_ID );

	ASSERT				(product);
	company->Close		();

	return				(product);
}

WeifenLuo::WinFormsUI::Docking::IDockContent^ window_ide::reload_content	(System::String^ persist_string)
{
	WeifenLuo::WinFormsUI::Docking::IDockContent^	result = nullptr;

	if (persist_string == "northstarcpp.editor.window_view")
	{
		result			= m_view;
		return			result;
	}

	result = get_editor_world().find_dock_content(persist_string);

	return				(result);
}