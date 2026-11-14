//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_OPERATION_AND_H_INCLUDED
#define DIALOG_OPERATION_AND_H_INCLUDED

#include "dialog_operation.h" 

#pragma managed(push, off)
namespace northstarcpp {
namespace dialog_editor {

	class dialog_operation_and : public dialog_operation 
	{
	public:
						dialog_operation_and();
		virtual			~dialog_operation_and();
		virtual bool	check				();
		virtual	void	load				(northstarcpp::configs::lua_config_value const& cfg);
		virtual	void	save				(northstarcpp::configs::lua_config_value cfg);

	private:
		static u32	m_created_counter;
	}; // class dialog_operation_and
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)
#endif // #ifndef DIALOG_OPERATION_AND_H_INCLUDED
