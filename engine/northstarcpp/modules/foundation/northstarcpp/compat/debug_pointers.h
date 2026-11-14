////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DEBUG_POINTERS_H_INCLUDED
#define NORTHSTARCPP_DEBUG_POINTERS_H_INCLUDED

#ifdef DEBUG
#	define NORTHSTARCPP_USE_DEBUG_POINTERS	1
#else // #ifdef DEBUG
#	define NORTHSTARCPP_USE_DEBUG_POINTERS	0
#endif // #ifdef DEBUG

#include <northstarcpp/compat/raw_ptr.h>
#include <northstarcpp/compat/non_null_ptr.h>
#include <northstarcpp/compat/loose_ptr.h>

#endif // #ifndef NORTHSTARCPP_DEBUG_POINTERS_H_INCLUDED