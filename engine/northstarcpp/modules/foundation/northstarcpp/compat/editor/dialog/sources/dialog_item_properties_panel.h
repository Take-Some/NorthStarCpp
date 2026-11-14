//-------------------------------------------------------------------------------------------
//	Created		: 17.12.2009
//	Author		: Kayla Verner

// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_ITEM_PROPERTIES_PANEL_H_INCLUDED
#define DIALOG_ITEM_PROPERTIES_PANEL_H_INCLUDED

using namespace System;
using namespace System::Windows::Forms;
using namespace Flobbster::Windows::Forms;
using namespace northstarcpp::editor::controls;
using namespace System::Drawing::Design;

using northstarcpp::editor::wpf_controls::property_grid_value_changed_event_args;

namespace northstarcpp {
namespace dialog_editor {
	ref class dialog_editor_impl;

	public ref class dialog_item_properties_panel : public northstarcpp::editor::controls::item_properties_panel_base
	{
		typedef northstarcpp::editor::controls::item_properties_panel_base super;
	public:
						dialog_item_properties_panel(dialog_editor_impl^ ed);
		virtual	void	show_properties				(Object^ object) override;

	protected:
		~dialog_item_properties_panel()
		{
			if(components)
				delete components;
		}

	private:
		void			on_node_property_changed	(System::Object^ sender, property_grid_value_changed_event_args^ e);

	private:
		System::ComponentModel::Container^	components;
		dialog_editor_impl^					m_owner;
	}; // ref class dialog_item_properties_panel
} // namespace dialog_editor
} // namespace northstarcpp
#endif // #ifndef DIALOG_ITEM_PROPERTIES_PANEL_H_INCLUDED