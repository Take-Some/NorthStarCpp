//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_ACTION_H_INCLUDED
#define DIALOG_ACTION_H_INCLUDED

#include <northstarcpp/compat/fixed_string.h>

#pragma managed(push, off)
namespace northstarcpp {
namespace dialog_editor {
	enum e_action_functions
	{
		no_act_func	= 0,
		give_info	= 1,
		give_item	= 2,
	};

	class dialog_action
	{
		friend ref class dialog_link_action;
	public:
								dialog_action		(u8 func=0, pstr par="");
								~dialog_action		();
				void			do_action			();	
				void			load				(northstarcpp::configs::lua_config_value const& cfg);
				void			save				(northstarcpp::configs::lua_config_value cfg);

	private:
		static	void			action_give_info	(pcstr info);
		static	void			action_give_item	(pcstr item_name);

	private:
		e_action_functions	m_function;
		pstr				m_param;
		static u32			m_created_counter;
	}; // class dialog_action
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)
#endif // #ifndef DIALOG_ACTION_H_INCLUDED
