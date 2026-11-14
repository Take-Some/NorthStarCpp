//-------------------------------------------------------------------------------------------
//	Created		: 20.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "item_properties_panel_base.h"
#include "property_grid.h"

using namespace System;
using namespace System::Collections::Generic;

namespace northstarcpp {
namespace editor {
namespace controls {

void item_properties_panel_base::in_constructor()
{
	m_property_grid_host = gcnew property_grid_host();
	m_property_grid_host->Dock = System::Windows::Forms::DockStyle::Fill;
	
	SuspendLayout();
	Controls->Add(m_property_grid_host);
	m_property_grid_host->selected_object = nullptr;
	ResumeLayout(false);
}

void item_properties_panel_base::show_properties(Object^ object)
{
	array<Object^>^ objects = dynamic_cast<array<Object^>^>(object);
	if(objects != nullptr)
		m_property_grid_host->selected_objects = objects;
	else
		m_property_grid_host->selected_object = object;
}

} // namespace controls
} // namespace editor
} // namespace northstarcpp