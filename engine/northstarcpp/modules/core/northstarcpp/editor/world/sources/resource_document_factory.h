////////////////////////////////////////////////////////////////////////////
//	Created		: 24.02.2010
//	Author		: Evgeniy Obertyukh
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RESOURCE_DOCUMENT_FACTORY_H_INCLUDED
#define RESOURCE_DOCUMENT_FACTORY_H_INCLUDED

namespace northstarcpp
{
	namespace editor
	{
		// move to resource_editor_base
		// void	set_view_panel_caption		(String^ caption);
		// void	set_properties_panel_caption(String^ caption);
		// void	set_editor_caption			(String^ caption);

		// pass tree_view_source on constructor
		// controls::tree_view_source^ get_resources_source		();	// => tree_view_source

		// rename class resource_document_factory
		public interface class resource_document_factory
		{
			virtual controls::document_base^	new_document	(controls::document_editor_base^ editor) = 0;
		}; // interface class resource_document_factory

		//public interface class resource_base
		//{
		//public:
		//	virtual controls::document_base^	get_new_document			(controls::document_editor_base^ editor);
		//	virtual controls::tree_view_source^ get_resources_source		();
		//	virtual String^						get_view_panel_name			();
		//	virtual String^						get_properties_panel_name	();
		//	virtual String^						get_editor_name				();
		//}; // class resource_base			
	} // namespace editor
} // namespace northstarcpp

#endif // #ifndef RESOURCE_DOCUMENT_FACTORY_H_INCLUDED