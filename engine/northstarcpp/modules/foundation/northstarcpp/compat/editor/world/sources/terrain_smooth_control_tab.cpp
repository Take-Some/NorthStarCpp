////////////////////////////////////////////////////////////////////////////
//	Created		: 14.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "terrain_smooth_control_tab.h"
#include "terrain_modifier_smooth.h"
#include "tool_terrain.h"

using northstarcpp::editor::terrain_smooth_control_tab;
using northstarcpp::editor::terrain_modifier_smooth;

void terrain_smooth_control_tab::in_constructor()
{
	terrain_modifier_smooth^ control	= m_tool->m_terrain_modifier_smooth;
	control->subscribe_on_activated		(gcnew activate_control_event(this, &terrain_smooth_control_tab::on_control_activated));
	control->subscribe_on_deactivated	(gcnew deactivate_control_event(this, &terrain_smooth_control_tab::on_control_deactivated));
}

void terrain_smooth_control_tab::on_control_activated(northstarcpp::editor::editor_control_base^ c)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER(c);
	Show						();
	sync						(true);
}

void terrain_smooth_control_tab::on_control_deactivated(northstarcpp::editor::editor_control_base^ c)
{
	Hide						();
	NORTHSTARCPP_UNREFERENCED_PARAMETER	(c);
}

void terrain_smooth_control_tab::sync(bool b_sync_ui)
{
	if(m_in_sync)
		return;

	m_in_sync = true;
	terrain_modifier_smooth^ control = m_tool->m_terrain_modifier_smooth;

	if(b_sync_ui)
	{
		radius_track_bar->fltValue		= control->radius;
		hardness_track_bar->fltValue	= control->hardness;
		distortion_track_bar->fltValue	= control->distortion;
		strength_track_bar->fltValue	= (float)control->strength;
	}else
	{
		control->radius					= radius_track_bar->fltValue;
		control->hardness				= hardness_track_bar->fltValue;
		control->distortion				= distortion_track_bar->fltValue;
		control->strength				= strength_track_bar->fltValue;
	}

	m_in_sync = false;
}

void terrain_smooth_control_tab::sync(System::Object^, System::EventArgs^)
{
	sync					(false);
}