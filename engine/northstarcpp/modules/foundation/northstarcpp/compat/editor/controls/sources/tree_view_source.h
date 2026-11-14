////////////////////////////////////////////////////////////////////////////
//	Created		: 15.12.2009
//	Author		: Evgeniy Obertyukh
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TREE_VIEW_SOURCE_H_INCLUDED
#define TREE_VIEW_SOURCE_H_INCLUDED

namespace northstarcpp
{
	namespace editor
	{
		namespace controls
		{
			public interface class tree_view_source
			{
			public:
				property tree_view^	parent
				{
					tree_view^			get	();
					void				set	(tree_view^ value);
				};

			public:
				void refresh();

			}; // class tree_view_source
		}//namespace controls
	}//namespace editor
}//namespace northstarcpp
#endif // #ifndef FILE_TREE_VIEW_SOURCE_H_INCLUDED