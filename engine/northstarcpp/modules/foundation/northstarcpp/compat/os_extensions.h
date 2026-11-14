////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_OS_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_OS_EXTENSIONS_H_INCLUDED

#include <northstarcpp/compat/macro_platform.h>
#include <northstarcpp/compat/type_extensions.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS || NORTHSTARCPP_PLATFORM_XBOX_360
#	define LPVOID					pvoid
#	define HANDLE					pvoid
#	define INVALID_HANDLE_VALUE		((HANDLE)((u64)(-1)))

#	define BOOL						int
#	define FALSE					0
#	define TRUE						1

#	define DLL_PROCESS_DETACH		0
#	define DLL_PROCESS_ATTACH		1

#	define APIENTRY					__stdcall

#	define HMODULE					pvoid

	struct HWND__;
#	define	HWND					HWND__*

#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS || NORTHSTARCPP_PLATFORM_XBOX_360

#include <northstarcpp/compat/os_functions.h>

#endif // #ifndef NORTHSTARCPP_OS_EXTENSIONS_H_INCLUDED