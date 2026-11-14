//-------------------------------------------------------------------------------------------
//	Created		: 14.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "add_language_form.h"
#include "dialog_editor.h"

using northstarcpp::dialog_editor::add_language_form;

void add_language_form::on_ok_clicked(System::Object^ , System::EventArgs^ )
{
	if(textBox1->Text!="")
		m_editor->add_language(textBox1->Text);

	this->Hide();
}

void add_language_form::on_cancel_clicked(System::Object^ , System::EventArgs^ )
{
	this->Hide();
}
