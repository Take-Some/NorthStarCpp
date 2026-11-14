////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_OS_INCLUDE_H_INCLUDED
#define NORTHSTARCPP_OS_INCLUDE_H_INCLUDED

#include <northstarcpp/compat/os_preinclude.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS

#	pragma warning ( push )
#	pragma warning ( disable : 4005 )		// because of the DELETE macro
#	include			<windows.h>
#	pragma warning ( pop )

#	undef DELETE
#	undef max
#	undef min

#	define IS_DEBUGGER_PRESENT				IsDebuggerPresent
#	define OUTPUT_DEBUG_STRING				OutputDebugString

#elif NORTHSTARCPP_PLATFORM_XBOX_360				// #if NORTHSTARCPP_PLATFORM_WINDOWS

#	pragma warning ( push )
#	pragma warning ( disable : 4005 )		// because of the DELETE macro
#	include			<xtl.h>
#	pragma warning ( pop )

#	pragma warning ( push )
#	pragma warning ( disable : 4995 )		// because of the DELETE macro
#	include			<d3dx9math.h>
#	pragma warning ( pop )

#	undef DELETE
#	undef max
#	undef min

#	ifdef DEBUG
#		include			<xbdm.h>
#		pragma comment	( lib, "xbdm.lib" )
#		define	IS_DEBUGGER_PRESENT			DmIsDebuggerPresent
#		define	OUTPUT_DEBUG_STRING			OutputDebugString
#	else // #ifdef DEBUG
		namespace northstarcpp {
			inline bool false_predicate( ) { return false; }
		}
#		define	IS_DEBUGGER_PRESENT			northstarcpp::false_predicate
#		define	OUTPUT_DEBUG_STRING( a )
#	endif // #ifdef DEBUG

#elif NORTHSTARCPP_PLATFORM_PS3						// #if NORTHSTARCPP_PLATFORM_PS3
	namespace northstarcpp {
		inline bool false_predicate( )		{ return false; }
		inline bool true_predicate( )		{ return true; }
	}
#	define	IS_DEBUGGER_PRESENT				northstarcpp::true_predicate
#	define OUTPUT_DEBUG_STRING( a )			::printf("%s",(a));
#else // #elif NORTHSTARCPP_PLATFORM_PS3

#	error please define your platform

#endif // #elif NORTHSTARCPP_PLATFORM_PS3

#if NORTHSTARCPP_PLATFORM_WINDOWS | NORTHSTARCPP_PLATFORM_XBOX_360
	COMPILE_ASSERT	(((HANDLE)(LONG_PTR)INVALID_HANDLE_VALUE_fake_value) == INVALID_HANDLE_VALUE, HANDLE_definition_in_windows_h_differs_from_what_is_expected_See_os_include_h);
	COMPILE_ASSERT	(sizeof(HMODULE_fake_type) == sizeof(HMODULE), HMODULE_definition_in_windows_h_differs_from_what_is_expected_See_os_include_h);
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS | NORTHSTARCPP_PLATFORM_XBOX_360

#endif // #ifndef NORTHSTARCPP_OS_INCLUDE_H_INCLUDED