////////////////////////////////////////////////////////////////////////////
//	Created		: 26.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "dependency_editor_bool_bool.h"

namespace northstarcpp
{
	namespace editor
	{
		namespace controls
		{
			void dependency_editor_bool_bool::in_constructor()
			{
				m_check_box->CheckedChanged += gcnew EventHandler(this, &dependency_editor_bool_bool::values_processor);
			}

			list_of_float^	dependency_editor_bool_bool::values::get		()
			{
				return gcnew list_of_float(gcnew array<Single>(1){(m_check_box->Checked)?1.0f:0.0f});
			}
			void			dependency_editor_bool_bool::values::set		(list_of_float^ values)
			{
				 if(values->Count > 0)
					m_check_box->Checked = values[0]==1;
			}
		}//namespace controls
	}//namespace editor
}//namespace northstarcpp