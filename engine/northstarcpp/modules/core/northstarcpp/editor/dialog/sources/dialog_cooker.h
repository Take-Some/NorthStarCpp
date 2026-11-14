//-------------------------------------------------------------------------------------------
//	Created		: 15.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#ifndef DIALOG_COOKER_H_INCLUDED
#define DIALOG_COOKER_H_INCLUDED

#pragma managed(push, off)

#include <northstarcpp/resources_cook_classes.h>

namespace northstarcpp {
namespace dialog_editor {
	class dialog;
	class dialog_cooker : public resources::translate_query_cook
	{
	public:
						dialog_cooker		();
		virtual void	translate_query		(northstarcpp::resources::query_result& parent);
		virtual void	delete_resource		(resources::unmanaged_resource* res);

	private:
				void	on_loaded			(resources::queries_result& result);
	}; // class dialog_cooker
} // namespace dialog_editor 
} // namespace northstarcpp 
#pragma managed(pop)
#endif // #ifndef DIALOG_COOKER_H_INCLUDED
