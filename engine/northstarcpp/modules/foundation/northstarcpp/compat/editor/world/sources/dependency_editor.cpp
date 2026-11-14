////////////////////////////////////////////////////////////////////////////
//	Created		: 26.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "dependency_editor.h"


namespace northstarcpp
{
	namespace editor
	{
		namespace controls
		{
			void dependency_editor::values_processor(Object^ sender, EventArgs^ e)
			{
				NORTHSTARCPP_UNREFERENCED_PARAMETERS(sender,e);

				if ( m_loading )
					return;

				m_selected_values->Clear();
				m_selected_values->AddRange(values);
			}
		}//namespace controls
	}//namespace editor
}//namespace northstarcpp
