////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_STRINGS_H_INCLUDED
#define NORTHSTARCPP_MACRO_STRINGS_H_INCLUDED

// NORTHSTARCPP_STRING_CONCAT macro
#if defined(NORTHSTARCPP_STRING_CONCAT) || defined(NORTHSTARCPP_STRING_CONCAT_HELPER)
#	error please do not define NORTHSTARCPP_STRING_CONCAT or NORTHSTARCPP_STRING_CONCAT_HELPER macros
#endif // #if defined(NORTHSTARCPP_STRING_CONCAT) || defined(NORTHSTARCPP_STRING_CONCAT_HELPER)

#define NORTHSTARCPP_STRING_CONCAT_HELPER(a,b)	a##b
#define NORTHSTARCPP_STRING_CONCAT(a,b)			NORTHSTARCPP_STRING_CONCAT_HELPER(a,b)

// NORTHSTARCPP_MAKE_STRING macro
#if defined(NORTHSTARCPP_MAKE_STRING) || defined(NORTHSTARCPP_MAKE_STRING_HELPER)
#	error please do not define NORTHSTARCPP_MAKE_STRING or NORTHSTARCPP_MAKE_STRING_HELPER macros
#endif // #if defined(NORTHSTARCPP_MAKE_STRING) || defined(NORTHSTARCPP_MAKE_STRING_HELPER)

#define NORTHSTARCPP_MAKE_STRING_HELPER(a)		#a
#define NORTHSTARCPP_MAKE_STRING(a)				NORTHSTARCPP_MAKE_STRING_HELPER(a)

#endif // #ifndef NORTHSTARCPP_MACRO_STRINGS_H_INCLUDED