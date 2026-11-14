////////////////////////////////////////////////////////////////////////////
//	Created		: 07.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef OUT_OF_MEMORY_HANDLER_TYPES_H_INCLUDED
#define OUT_OF_MEMORY_HANDLER_TYPES_H_INCLUDED

#define USE_OUT_OF_MEMORY_HANDLER

#ifdef USE_OUT_OF_MEMORY_HANDLER
	typedef void const* out_of_memory_handler_parameter_type;
	typedef void (__stdcall *out_of_memory_handler_type)	(void*, out_of_memory_handler_parameter_type, int);
#endif // #ifdef USE_OUT_OF_MEMORY_HANDLER

#endif // #ifndef OUT_OF_MEMORY_HANDLER_TYPES_H_INCLUDED