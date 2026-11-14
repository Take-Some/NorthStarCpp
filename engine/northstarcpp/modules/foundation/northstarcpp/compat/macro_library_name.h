////////////////////////////////////////////////////////////////////////////
//	Created 	: 06.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_LIBRARY_NAME_H_INCLUDED
#define NORTHSTARCPP_MACRO_LIBRARY_NAME_H_INCLUDED

#include <northstarcpp/compat/macro_extensions.h>

#ifdef NORTHSTARCPP_LIBRARY_PREFIX
#	error please do not define NORTHSTARCPP_LIBRARY_PREFIX macro
#endif // #ifdef NORTHSTARCPP_LIBRARY_PREFIX

#ifdef _MSC_VER
#	define NORTHSTARCPP_LIBRARY_PREFIX
#else // #ifdef _MSC_VER
#	define NORTHSTARCPP_LIBRARY_PREFIX
#endif // #ifdef _MSC_VER

#ifdef NORTHSTARCPP_STATIC_LIBRARIES
#	ifdef MASTER_GOLD
#		define NORTHSTARCPP_STATIC_ID		-static-gold
#	else // #ifdef MASTER_GOLD
#		define NORTHSTARCPP_STATIC_ID		-static
#	endif // #ifdef MASTER_GOLD
#else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_STATIC_ID
#endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES

#ifdef DEBUG
#	define NORTHSTARCPP_DEBUG_LIBRARIES
#	define NORTHSTARCPP_CONFIGURATION_ID	-debug
#else // #ifdef DEBUG
#	define NORTHSTARCPP_CONFIGURATION_ID
#endif // #ifdef DEBUG

#ifdef _MSC_VER
#	define NORTHSTARCPP_LIBRARY_NAME(library, extension)	\
		NORTHSTARCPP_MAKE_STRING(\
			NORTHSTARCPP_STRING_CONCAT(\
				NORTHSTARCPP_LIBRARY_PREFIX,\
				NORTHSTARCPP_STRING_CONCAT(\
					library,\
					NORTHSTARCPP_STRING_CONCAT(\
						NORTHSTARCPP_STATIC_ID,\
						NORTHSTARCPP_STRING_CONCAT(\
							NORTHSTARCPP_CONFIGURATION_ID,\
							NORTHSTARCPP_STRING_CONCAT(\
								.,\
								extension\
							)\
						)\
					)\
				)\
			)\
		)
#else // #ifdef _MSC_VER
#	if defined(__SNC__)
#		define NORTHSTARCPP_COMPILER_ID	-snc
#	elif defined(__GCC__) // #if defined(__SNC__)
#		define NORTHSTARCPP_COMPILER_ID	-gcc
#	else // #elif defined(__GCC__)
#		error define your NORTHSTARCPP_COMPILER_ID here
#	endif // #if defined(__SNC__)

#	define NORTHSTARCPP_LIBRARY_NAME(library, extension)	\
		NORTHSTARCPP_MAKE_STRING(\
			NORTHSTARCPP_STRING_CONCAT(\
				NORTHSTARCPP_LIBRARY_PREFIX,\
				NORTHSTARCPP_STRING_CONCAT(\
					library,\
					NORTHSTARCPP_STRING_CONCAT(\
						NORTHSTARCPP_COMPILER_ID,\
						NORTHSTARCPP_STRING_CONCAT(\
							NORTHSTARCPP_STATIC_ID,\
							NORTHSTARCPP_CONFIGURATION_ID\
						)\
					)\
				)\
			)\
		)
#endif // #ifdef _MSC_VER

#endif // #ifndef NORTHSTARCPP_MACRO_LIBRARY_NAME_H_INCLUDED