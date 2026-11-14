//-------------------------------------------------------------------------------------------
//	Created		: 25.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

using namespace System;
using namespace System::Collections;
using namespace System::ComponentModel;

#include "dialog_link.h"
#include "dialog_expression.h"
#include "dialog_action.h"

namespace northstarcpp {
namespace dialog_editor {
	ref class dialog_link_ui_type_editor;
	ref class dialog_link_action;
	ref class dialog_link_precondition;

	public ref class dialog_graph_link : public northstarcpp::editor::controls::hypergraph::link
	{
		typedef northstarcpp::editor::controls::hypergraph::link					super;
		typedef northstarcpp::editor::controls::hypergraph::connection_point		connection_point;

	public:
		dialog_graph_link	(connection_point^ src, connection_point^ dst, bool create_dialog_link);
		~dialog_graph_link	();

		[CategoryAttribute("Link properties"), DisplayNameAttribute("Preconditions"),
			EditorAttribute(dialog_link_ui_type_editor::typeid,System::Drawing::Design::UITypeEditor::typeid)]
		property dialog_expression*	prec
		{
			dialog_expression*		get		() {return m_link->root_precondition();};
		};

		[CategoryAttribute("Link properties"), DisplayNameAttribute("Actions"),
			EditorAttribute(dialog_link_ui_type_editor::typeid,System::Drawing::Design::UITypeEditor::typeid)]
		property vector<dialog_action*>* act
		{
			vector<dialog_action*>*	get		() {return m_link->actions();};
		};

		property dialog_link* m_link;

		void						set_preds_and_acts(dialog_link_precondition^ prec_root, Generic::List<dialog_link_action^>^ act_list);
		void						load	(northstarcpp::configs::lua_config_value const& cfg){m_link->load(cfg);};
		void						save	(northstarcpp::configs::lua_config_value cfg)		{m_link->save(cfg);};
	}; // ref class dialog_graph_link
} // namespace dialog_editor
} // namespace northstarcpp