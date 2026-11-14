//-------------------------------------------------------------------------------------------
//	Created		: 17.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "dialog_item_properties_panel.h"
#include "dialog_editor.h"
#include "dialog_graph_link.h"

using namespace System;
using northstarcpp::editor::controls::property_grid;
using northstarcpp::dialog_editor::dialog_item_properties_panel;
using northstarcpp::dialog_editor::dialog_editor_impl;
using northstarcpp::dialog_editor::dialog_graph_link;

using northstarcpp::editor::wpf_controls::property_grid_value_changed_event_handler;

dialog_item_properties_panel::dialog_item_properties_panel(dialog_editor_impl^ ed)
:northstarcpp::editor::controls::item_properties_panel_base(ed->multidocument_base),
m_owner(ed)
{
	m_property_grid_host->property_value_changed += gcnew property_grid_value_changed_event_handler(this, &dialog_item_properties_panel::on_node_property_changed);
	m_property_grid_host->auto_update = true;
	HideOnClose = true;
}

void dialog_item_properties_panel::on_node_property_changed(System::Object^ sender, property_grid_value_changed_event_args^ e)
{
	m_owner->on_node_property_changed(sender, e);
}

void dialog_item_properties_panel::show_properties(Object^ object)
{
	if(dynamic_cast<dialog_graph_link^>(object))
	{
		m_property_grid_host->browsable_attributes->Clear();
		m_property_grid_host->browsable_attributes->Add(
			gcnew System::ComponentModel::CategoryAttribute("Link properties")
		);
	}
	else if(object!=nullptr)
	{
		m_property_grid_host->browsable_attributes->Clear();
		m_property_grid_host->browsable_attributes->Add(
			gcnew System::ComponentModel::CategoryAttribute("Phrase properties")
		);
	}
	super::show_properties(object);
}
