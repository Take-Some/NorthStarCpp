//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#include "pch.h"
#include "dialog_operation.h"
#include "dialog_expression.h"

#pragma unmanaged
using northstarcpp::dialog_editor::dialog_operation;
using northstarcpp::dialog_editor::dialog_expression;

dialog_operation::dialog_operation()
{
	m_childs = vector<dialog_expression*>();
}

dialog_operation::~dialog_operation()
{

}

void dialog_operation::destroy()
{
	vector<dialog_expression*>::iterator it = m_childs.begin();
	for(; it!=m_childs.end(); ++it)
		(*it)->destroy();

	DELETE(this);
}
