////////////////////////////////////////////////////////////////////////////
//	Created		: 03.03.2010
//	Author		: Evgeniy Obertyukh
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef DOCUMENT_EVENT_ARGS_H_INCLUDED
#define DOCUMENT_EVENT_ARGS_H_INCLUDED

using namespace System;

namespace northstarcpp
{
	namespace editor
	{
		namespace controls 
		{

			public ref class document_event_args : EventArgs
			{
			public:
				document_event_args(document_base^ set_document):document(set_document)
				{}

				document_base^ document;

			}; // class document_event_args

		} // namespace controls
	} // namespace editor
} // namespace northstarcpp

#endif // #ifndef DOCUMENT_EVENT_ARGS_H_INCLUDED