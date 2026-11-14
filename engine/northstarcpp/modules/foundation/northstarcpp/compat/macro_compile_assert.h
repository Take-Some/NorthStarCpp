////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_STATIC_CHECK_H_INCLUDED
#define NORTHSTARCPP_MACRO_STATIC_CHECK_H_INCLUDED

#ifdef COMPILE_ASSERT
#	error do not define COMPILE_ASSERT macro
#endif // #if defined(WINDOWS_32) && defined(XBOX_360)

#define COMPILE_ASSERT(expr, msg)	typedef char ERROR_##msg[1][(expr)]

#endif // #ifndef NORTHSTARCPP_MACRO_STATIC_CHECK_H_INCLUDED