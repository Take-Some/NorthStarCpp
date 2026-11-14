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

#include <northstarcpp/raw_ptr.h>
#include <northstarcpp/non_null_ptr.h>
#include <northstarcpp/loose_ptr.h>

#endif // #ifndef NORTHSTARCPP_DEBUG_POINTERS_H_INCLUDED