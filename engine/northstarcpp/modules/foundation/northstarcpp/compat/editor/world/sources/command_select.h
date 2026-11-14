////////////////////////////////////////////////////////////////////////////
//	Created		: 01.04.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_SELECT_H_INCLUDED
#define COMMAND_SELECT_H_INCLUDED

#include "project_defines.h"

namespace northstarcpp {
namespace editor {

ref class level_editor;

public ref class command_select :	public northstarcpp::editor_base::command
{
public:
	command_select	( level_editor^ le);
	command_select	( level_editor^ le, object_list^ objects, enum_selection_method method/* = enum_selection_method_set*/ );
	command_select	( level_editor^ le, object_base^ object, enum_selection_method method /*= enum_selection_method_set*/ );
	command_select	( level_editor^ le, id_list^ objects, enum_selection_method method /*= enum_selection_method_set*/ );
	command_select	( level_editor^ le, u32 id, enum_selection_method method /*= enum_selection_method_set*/ );

	virtual bool commit		() override;
	virtual void rollback	() override;
			bool work		( enum_selection_method method );

private:
	level_editor^	m_level_editor;
	id_list^		m_objects;
	enum_selection_method	m_selection_method;
}; // class command_select

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef COMMAND_SELECT_H_INCLUDED