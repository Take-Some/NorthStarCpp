////////////////////////////////////////////////////////////////////////////
//	Created		: 28.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_COMPILER_H_INCLUDED
#define NORTHSTARCPP_MACRO_COMPILER_H_INCLUDED

#include <northstarcpp/macro_platform.h>

#if defined(_MSC_VER)
#	if _MSC_VER >= 1100
#		define NORTHSTARCPP_NOVTABLE									__declspec( novtable )
#	else // #if _MSC_VER >= 1100
#		define NORTHSTARCPP_NOVTABLE
#	endif // #if _MSC_VER >= 1100

#	define NORTHSTARCPP_ALIGN( alignment )								__declspec( align(alignment) )
#	define NORTHSTARCPP_DLL_IMPORT										__declspec( dllimport )
#	define NORTHSTARCPP_DLL_EXPORT										__declspec( dllexport )
//#	define NORTHSTARCPP_ASSUME( expression )							__assume( expression )
#	define NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( type )			virtual	~type	( ) = 0 { }
#	define ALLOCA												_alloca
#elif defined(__GNUC__)
#	define NORTHSTARCPP_NOVTABLE
#	define NORTHSTARCPP_ALIGN( alignment )								__attribute__( (aligned(alignment)) )
#	define NORTHSTARCPP_DLL_IMPORT										__attribute__( (dllimport) )
#	define NORTHSTARCPP_DLL_EXPORT										__attribute__( (dllexport) )
#	define NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( type )			virtual	~type	( ) {}

#	define ALLOCA __builtin_alloca

#	define __FUNCSIG__											"<unknown function signature>"
#else // #elif NORTHSTARCPP_PLATFORM_PS3
#	error please define macros for your compiler here
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS | NORTHSTARCPP_PLATFORM_XBOX_360

#endif // #ifndef NORTHSTARCPP_MACRO_COMPILER_H_INCLUDED