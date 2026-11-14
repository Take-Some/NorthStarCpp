//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_LINK_H_INCLUDED
#define DIALOG_LINK_H_INCLUDED

#pragma managed(push, off)
namespace northstarcpp {
namespace dialog_editor {
	class dialog_node_base;
	class dialog_expression;
	class dialog_action;

	class dialog_link
	{
	public:
								dialog_link				(dialog_node_base* chld);
								~dialog_link			();
		void					set_root_precondition	(dialog_expression* new_root)	{m_root_precondition = new_root;};
		dialog_expression*		root_precondition		()								{return m_root_precondition;};
		void					add_action				(dialog_action* new_action)		{m_actions_list.push_back(new_action);};
		vector<dialog_action*>*	actions					()								{return &m_actions_list;};
		void					change_child			(dialog_node_base* new_child)	{m_child = new_child;};
		dialog_node_base*		child					()								{return m_child;};
		bool					check_preconditions		();
		void					do_actions				();
		void					load					(northstarcpp::configs::lua_config_value const& cfg);
		void					save					(northstarcpp::configs::lua_config_value cfg);

	public:
		dialog_link*			m_next_link;

	private:
		dialog_node_base*		m_child;
		dialog_expression*		m_root_precondition;
		vector<dialog_action*>	m_actions_list;
		static u32				m_created_counter;
	}; // class dialog_link
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)
#endif // #ifndef DIALOG_LINK_H_INCLUDED
