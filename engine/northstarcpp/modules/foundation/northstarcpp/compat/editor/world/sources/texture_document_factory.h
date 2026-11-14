////////////////////////////////////////////////////////////////////////////
//	Created		: 24.02.2010
//	Author		: Evgeniy Obertyukh
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEXTURE_DOCUMENT_FACTORY_H_INCLUDED
#define TEXTURE_DOCUMENT_FACTORY_H_INCLUDED

#include "resource_document_factory.h"
#include "texture_document.h"

namespace northstarcpp
{
	namespace editor
	{
		ref class texture_document_factory : resource_document_factory
		{
		public:
			virtual controls::document_base^	new_document			(controls::document_editor_base^ editor){return gcnew texture_document(editor);}
		}; // class texture_resource
	} // namespace editor
} // namespace northstarcpp

#endif // #ifndef TEXTURE_DOCUMENT_FACTORY_H_INCLUDED