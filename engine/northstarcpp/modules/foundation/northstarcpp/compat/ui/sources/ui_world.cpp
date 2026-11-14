////////////////////////////////////////////////////////////////////////////
//	Created		: 30.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "ui_world.h"
#include "ui_text_edit.h"
#include <northstarcpp/compat/render/world.h>

//static ui::dialog* d			= NULL;

ui_world::ui_world(engine& engine, northstarcpp::input::world& input, northstarcpp::render::world& render)
:	m_engine			(engine),
	m_input				(input),
	m_render			(render),
	m_base_screen_size	(1024.0f, 768.0f)
{
//	m_dialog_holder		= NEW (ui_dialog_holder)( *this );
	m_timer.start	();
}

ui_world::~ui_world()
{
//	DELETE (m_dialog_holder);
}

void ui_world::tick()
{
//	m_dialog_holder->update			();
//	m_dialog_holder->draw			(m_render);
}

void ui_world::clear_resources		( )
{
}

void ui_world::on_device_reset()
{
	m_font_manager.on_device_reset();
}
