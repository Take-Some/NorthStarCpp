//-------------------------------------------------------------------------------------------
//	Created		: 13.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef STRING_TABLE_ID_OPTIONS_H_INCLUDED
#define STRING_TABLE_ID_OPTIONS_H_INCLUDED

#include "string_table_ids_storage.h"
#pragma managed(push, off)

namespace northstarcpp {
namespace dialog_editor {
namespace string_tables {
	class string_table_id_options
	{
		typedef map<pstr, pstr, northstarcpp::dialog_editor::string_tables::strings_compare_predicate> batch_file_names_type;
	public:
		enum e_string_table_category
		{
			none		= (1<<0),
			dialogs		= (1<<1),
			items		= (1<<2),
			tasks		= (1<<3),
			ui			= (1<<4),
			locations	= (1<<5),
		};

								string_table_id_options	();
								~string_table_id_options();

		void					load					(northstarcpp::configs::lua_config_value const& cfg);
		void					save					(northstarcpp::configs::lua_config_value cfg);

		e_string_table_category	category				()			{return m_category;};
		void					set_category			(u32 cat)	{m_category=(e_string_table_category)cat;};

		u32						references_count		()			{return m_references_count;};
		void					set_references_count	(u32 rc)	{R_ASSERT(rc>=0);m_references_count=rc;};

		pcstr					batch_file_name			(pcstr loc_name);
		void					set_batch_file_name		(pcstr loc_name, pcstr fn);
		void					clear_batch_file_names	();

	private:
		e_string_table_category m_category;
		batch_file_names_type*	m_batch_file_names;
		u32						m_references_count;
	}; // class string_table_id_options
} // namespace string_tables 
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)

#endif // #ifndef STRING_TABLE_ID_OPTIONS_H_INCLUDED
