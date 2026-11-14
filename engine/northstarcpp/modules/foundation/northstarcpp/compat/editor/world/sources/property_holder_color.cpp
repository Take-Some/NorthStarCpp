////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_container.h"
#include "property_color.h"
#include "property_color_reference.h"
#include "color_holder.h"

ref class property_editor_color;
ref class property_converter_color;

using Flobbster::Windows::Forms::PropertySpec;


northstarcpp::editor::property_value* property_holder::add_color				(
	System::String^ identifier,
	System::String^ category,
	System::String^ description,
	color const& default_value,
	northstarcpp::configs::lua_config_value const& config,
	readonly_enum const& read_only,
	notify_parent_on_change_enum const& notify_parent,
	password_char_enum const& password,
	refresh_grid_on_change_enum const& refresh_grid
	)
{
	northstarcpp::editor::color_holder^ c_holder = gcnew northstarcpp::editor::color_holder(identifier, config);
	return add_color		(
		identifier,
		category,
		description,
		default_value,
		gcnew color_getter_type(c_holder, &northstarcpp::editor::color_holder::get),
		gcnew color_setter_type(c_holder, &northstarcpp::editor::color_holder::set),
		read_only,
		notify_parent,
		password,
		refresh_grid);
}
northstarcpp::editor::property_value* property_holder::add_color				(
		System::String^ identifier,
		System::String^ category,
		System::String^ description,
		color const& default_value,
		color_getter_type^ getter,
		color_setter_type^ setter,
		readonly_enum const& read_only,
		notify_parent_on_change_enum const& notify_parent,
		password_char_enum const& password,
		refresh_grid_on_change_enum const& refresh_grid
	)
{
	PropertySpec^				spec =
		gcnew PropertySpec(
			identifier,
			Color::typeid,
			category,
			description,
			Color(default_value.r, default_value.g, default_value.b),
			property_editor_color::typeid,
			property_converter_color::typeid
		);
	m_container->add_property	(
		spec,
		gcnew property_color(
			getter,
			setter,
			spec->Attributes,
			read_only,
			notify_parent,
			password,
			refresh_grid
		),
		read_only,
		notify_parent,
		password,
		refresh_grid
	);

	return						(nullptr);
}
/*
northstarcpp::editor::property_value* property_holder::add_color				(
		System::String^ identifier,
		System::String^ category,
		System::String^ description,
		color const& default_value,
		color& value,
		readonly_enum const& read_only,
		notify_parent_on_change_enum const& notify_parent,
		password_char_enum const& password,
		refresh_grid_on_change_enum const& refresh_grid
	)
{
	PropertySpec^				spec =
		gcnew PropertySpec(
			identifier,
			Color::typeid,
			category,
			description,
			Color(default_value.r, default_value.g, default_value.b),
			property_editor_color::typeid,
			property_converter_color::typeid
		);
	m_container->add_property	(
		spec,
		gcnew property_color_reference(
			value,
			spec->Attributes,
			read_only,
			notify_parent,
			password,
			refresh_grid
		),
		read_only,
		notify_parent,
		password,
		refresh_grid
	);

	return						(nullptr);
}
*/