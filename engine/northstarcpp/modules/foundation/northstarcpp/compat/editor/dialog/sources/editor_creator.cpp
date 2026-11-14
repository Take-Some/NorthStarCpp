//-------------------------------------------------------------------------------------------
//	Created		: 10.06.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "editor_creator.h"
#include "dialog_editor.h"
namespace northstarcpp{
namespace dialog_editor{

northstarcpp::editor_base::editor_base^ editor_creator::create_editor(System::String^ resources_path)
{
	m_ed = gcnew dialog_editor_impl(resources_path);
	m_ed->initialize();
	m_ed->load(nullptr);
	m_ed->load_settings();
	return m_ed;
}

void editor_creator::destroy_dialog_editor()
{
	//	delete m_ed;
}

void editor_creator::memory_allocator(northstarcpp::dialog_editor::allocator_type* allocator)
{
	ASSERT(!g_allocator);
	g_allocator = allocator;
}
} //namespace dialog_editor
} //namespace northstarcpp

