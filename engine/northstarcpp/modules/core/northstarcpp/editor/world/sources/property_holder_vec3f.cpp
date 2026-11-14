////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.12.2007
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "property_container.h"
#include "property_vec3f.h"
#include "float3_holder.h"
//#include "property_vec3f_reference.h"

ref class property_converter_vec3f;
ref class property_converter_vec2f;

using Flobbster::Windows::Forms::PropertySpec;
using System::String;

northstarcpp::editor::property_value* property_holder::add_vec3f				(
	System::String^ identifier,
	System::String^ category,
	System::String^ description,
	float3 const& default_value,
	northstarcpp::configs::lua_config_value const& config,
	readonly_enum const& read_only,
	notify_parent_on_change_enum const& notify_parent,
	password_char_enum const& password,
	refresh_grid_on_change_enum const& refresh_grid
	)
{
	northstarcpp::editor::float3_holder^ f3_holder = gcnew northstarcpp::editor::float3_holder(identifier, config); 
	return add_vec3f(
		identifier,
		category,
		description,
		default_value,
		gcnew float3_getter_type(f3_holder, &northstarcpp::editor::float3_holder::get),
		gcnew float3_setter_type(f3_holder, &northstarcpp::editor::float3_holder::set),
		read_only,
		notify_parent,
		password,
		refresh_grid);
}

northstarcpp::editor::property_value* property_holder::add_vec3f				(
		System::String^ identifier,
		System::String^ category,
		System::String^ description,
		float3 const& default_value,
		float3_getter_type^ getter,
		float3_setter_type^ setter,
		readonly_enum const& read_only,
		notify_parent_on_change_enum const& notify_parent,
		password_char_enum const& password,
		refresh_grid_on_change_enum const& refresh_grid
	)
{
	m_container->add_property	(
		gcnew PropertySpec(
			identifier,
			Vec3f::typeid,
			category,
			description,
			Vec3f(default_value.x, default_value.y, default_value.z),
			(String^)nullptr,
			property_converter_vec3f::typeid
		),
		gcnew property_vec3f(
			getter,
			setter,
			default_value,
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

northstarcpp::editor::property_value* property_holder::add_vec2f				(
		System::String^ identifier,
		System::String^ category,
		System::String^ description,
		float2 const& default_value,
		float2_getter_type^ getter,
		float2_setter_type^ setter,
		readonly_enum const& read_only,
		notify_parent_on_change_enum const& notify_parent,
		password_char_enum const& password,
		refresh_grid_on_change_enum const& refresh_grid
	)
{
	m_container->add_property	(
		gcnew PropertySpec(
			identifier,
			Vec2f::typeid,
			category,
			description,
			Vec2f(default_value.x, default_value.y),
			(String^)nullptr,
			property_converter_vec2f::typeid
		),
		gcnew property_vec2f(
			getter,
			setter,
			default_value,
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