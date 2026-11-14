////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_BUILDING_H_INCLUDED
#define NORTHSTARCPP_MACRO_BUILDING_H_INCLUDED

#ifdef NORTHSTARCPP_BUILDING
#	error do not define NORTHSTARCPP_BUILDING macro
#endif // #if NORTHSTARCPP_CORE_BUILDING

#define NORTHSTARCPP_BUILDING

#ifndef NORTHSTARCPP_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_CORE_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		if NORTHSTARCPP_CORE_BUILDING
#			define NORTHSTARCPP_CORE_API				NORTHSTARCPP_DLL_EXPORT
#		else // #if NORTHSTARCPP_CORE_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_CORE_API			NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_CORE_API			NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #if NORTHSTARCPP_CORE_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_API

#endif // #ifndef NORTHSTARCPP_MACRO_BUILDING_H_INCLUDED