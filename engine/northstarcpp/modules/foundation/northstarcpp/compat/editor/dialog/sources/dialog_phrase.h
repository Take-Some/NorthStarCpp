//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_PHRASE_H_INCLUDED
#define DIALOG_PHRASE_H_INCLUDED

#include "dialog_node_base.h"
#include "dialog_node_visitors.h"

#pragma managed(push, off)

namespace northstarcpp {
namespace dialog_editor {
	class dialog_node_base;
	class dialog_phrase;
	class dialog_node_gatherer_visitor;
	class dialog_node_giver_visitor;

	class dialog_phrase : public dialog_node_base
	{
	public:
						dialog_phrase		(pstr str_id=NULL);
		virtual			~dialog_phrase		();
				void	set_string_table_id	(pcstr new_str_id);
				pcstr	string_table_id		()										{return m_str_id;};
		virtual	void	set_text			(pcstr new_text);
		virtual pcstr	text				();
		virtual	void	accept				(dialog_node_gatherer_visitor* visitor) {visitor->visit(this);};
		virtual	void	accept				(dialog_node_giver_visitor* visitor)	{visitor->visit(this);};

//		virtual	void	recalculate_memory_usage_impl () { m_memory_usage_self.unmanaged = get_size(); }

	private:
		pstr		m_str_id;
		static u32	m_created_counter;
	}; // class dialog_phrase
} // namespace dialog_editor
} // namespace northstarcpp
#pragma managed(pop)

#endif // #ifndef DIALOG_PHRASE_H_INCLUDED
