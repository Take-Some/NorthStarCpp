////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_PLATFORM_H_INCLUDED
#define NORTHSTARCPP_MACRO_PLATFORM_H_INCLUDED

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_PLATFORM_WINDOWS_32
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_PLATFORM_WINDOWS_32
#	error do not define NORTHSTARCPP_PLATFORM_WINDOWS_32 macro
#endif // #ifdef NORTHSTARCPP_PLATFORM_WINDOWS_32

#if !defined(_WIN64) && !defined(WIN64) && (defined(_WIN32) || defined(WIN32))
#	define NORTHSTARCPP_PLATFORM_WINDOWS_32	1
#else // #ifdef WIN32
#	define NORTHSTARCPP_PLATFORM_WINDOWS_32	0
#endif // #ifdef WIN32

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_PLATFORM_WINDOWS_64
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_PLATFORM_WINDOWS_64
#	error do not define NORTHSTARCPP_PLATFORM_WINDOWS_64 macro
#endif // #ifdef NORTHSTARCPP_PLATFORM_WINDOWS_64

#if defined(_WIN64) || defined(WIN64)
#	define NORTHSTARCPP_PLATFORM_WINDOWS_64	1
#else // #ifdef WIN32
#	define NORTHSTARCPP_PLATFORM_WINDOWS_64	0
#endif // #ifdef WIN32

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_PLATFORM_WINDOWS
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_PLATFORM_WINDOWS
#	error do not define NORTHSTARCPP_PLATFORM_WINDOWS macro
#endif // #ifdef NORTHSTARCPP_PLATFORM_WINDOWS

#if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_WINDOWS_64
#	define NORTHSTARCPP_PLATFORM_WINDOWS	1
#else // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_WINDOWS_64
#	define NORTHSTARCPP_PLATFORM_WINDOWS	0
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_WINDOWS_64

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_PLATFORM_XBOX_360
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_PLATFORM_XBOX_360
#	error do not define NORTHSTARCPP_PLATFORM_XBOX_360 macro
#endif // #ifdef NORTHSTARCPP_CORE_BUILDING

#ifdef _XBOX
#	if _XBOX_VER < 200 
#		error engine does not support XBox versions earlier to XBox 360
#	endif // #if _XBOX_VER < 200 
#	define NORTHSTARCPP_PLATFORM_XBOX_360	1
#	define NORTHSTARCPP_STATIC_LIBRARIES	1
#else // #ifdef _XBOX
#	define NORTHSTARCPP_PLATFORM_XBOX_360	0
#endif // #ifdef _XBOX

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_PLATFORM_PS3
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_PLATFORM_PS3
#	error do not define NORTHSTARCPP_PLATFORM_PS3 macro
#endif // #ifdef NORTHSTARCPP_CORE_BUILDING

#ifdef SN_TARGET_PS3
#	define NORTHSTARCPP_PLATFORM_PS3		1
#else // #ifdef SN_TARGET_PS3
#	define NORTHSTARCPP_PLATFORM_PS3		0
#endif // #ifdef SN_TARGET_PS3

////////////////////////////////////////////////////////////////////////////
// NORTHSTARCPP_LITTLE_ENDIAN / NORTHSTARCPP_BIG_ENDIAN
////////////////////////////////////////////////////////////////////////////
#ifdef NORTHSTARCPP_LITTLE_ENDIAN
#	error do not define NORTHSTARCPP_LITTLE_ENDIAN macro
#endif // #ifdef NORTHSTARCPP_CORE_BUILDING

#ifdef NORTHSTARCPP_BIG_ENDIAN
#	error do not define NORTHSTARCPP_BIG_ENDIAN macro
#endif // #ifdef NORTHSTARCPP_CORE_BUILDING

#if NORTHSTARCPP_PLATFORM_WINDOWS 
#	define	NORTHSTARCPP_LITTLE_ENDIAN		1
#	define	NORTHSTARCPP_BIG_ENDIAN			0
#elif NORTHSTARCPP_PLATFORM_XBOX_360		// #if NORTHSTARCPP_PLATFORM_WINDOWS 
#	define	NORTHSTARCPP_LITTLE_ENDIAN		0
#	define	NORTHSTARCPP_BIG_ENDIAN			1
#elif NORTHSTARCPP_PLATFORM_PS3				// #elif NORTHSTARCPP_PLATFORM_XBOX_360
#	define	NORTHSTARCPP_LITTLE_ENDIAN		0
#	define	NORTHSTARCPP_BIG_ENDIAN			1
#endif								// #elif NORTHSTARCPP_PLATFORM_PS3

#if NORTHSTARCPP_LITTLE_ENDIAN & NORTHSTARCPP_BIG_ENDIAN
#	error invalid platform capabilities specified
#endif // #if NORTHSTARCPP_LITTLE_ENDIAN & NORTHSTARCPP_BIG_ENDIAN

////////////////////////////////////////////////////////////////////////////
// check macros
////////////////////////////////////////////////////////////////////////////
#if NORTHSTARCPP_PLATFORM_WINDOWS_32
#	if NORTHSTARCPP_PLATFORM_WINDOWS_64 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_PS3
#		error you cannot specify several target platforms
#	endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_64 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_PS3
#elif NORTHSTARCPP_PLATFORM_WINDOWS_64		// #if NORTHSTARCPP_PLATFORM_WINDOWS_32
#	if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_PS3
#		error you cannot specify several target platforms
#	endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_PS3
#elif NORTHSTARCPP_PLATFORM_XBOX_360			// #elif NORTHSTARCPP_PLATFORM_WINDOWS_64
#	if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_WINDOWS_64 | NORTHSTARCPP_PLATFORM_PS3
#		error you cannot specify several target platforms
#	endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_WINDOWS_64 | NORTHSTARCPP_PLATFORM_PS3
#elif NORTHSTARCPP_PLATFORM_PS3				// #elif NORTHSTARCPP_PLATFORM_XBOX_360
#	if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_XBOX_360
#		error you cannot specify several target platforms
#	endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32 | NORTHSTARCPP_PLATFORM_XBOX_360 | NORTHSTARCPP_PLATFORM_XBOX_360
#else								// #elif NORTHSTARCPP_PLATFORM_PS3
#		error unknown target platform!
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_32

#endif // #ifndef NORTHSTARCPP_MACRO_PLATFORM_H_INCLUDED