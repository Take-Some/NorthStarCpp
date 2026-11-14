//-------------------------------------------------------------------------------------------
//	Created		: 16.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_EXPRESSION_H_INCLUDED
#define DIALOG_EXPRESSION_H_INCLUDED

#pragma managed(push, off)
namespace northstarcpp {
namespace dialog_editor {
	class dialog_expression
	{
	public:
		virtual	~dialog_expression(){};
		virtual bool	check	()=0;
		virtual	void	load	(northstarcpp::configs::lua_config_value const& cfg)=0;
		virtual	void	save	(northstarcpp::configs::lua_config_value cfg)=0;
		virtual	void	destroy	()=0;
	}; // class dialog_expression
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)
#endif // #ifndef DIALOG_EXPRESSION_H_INCLUDED
