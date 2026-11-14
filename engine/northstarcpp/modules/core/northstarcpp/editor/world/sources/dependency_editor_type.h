////////////////////////////////////////////////////////////////////////////
//	Created		: 26.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef DEPENDENCY_EDITOR_TYPE_H_INCLUDED
#define DEPENDENCY_EDITOR_TYPE_H_INCLUDED

namespace northstarcpp
{
	namespace editor
	{
		typedef System::Collections::Generic::List<float>	list_of_float;

		namespace controls
		{
			enum class dependency_editor_type
			{
				bool_to_bool,
				bool_to_float,
				float_to_bool,
				flot_to_float
			}; // class dependency_editor_type
		}//namespace controls
	}//namespace editor
}//namespace northstarcpp
#endif // #ifndef DEPENDENCY_EDITOR_TYPE_H_INCLUDED