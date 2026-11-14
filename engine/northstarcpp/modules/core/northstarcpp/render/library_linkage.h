////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_LIBRARY_LINKAGE_H_INCLUDED
#define NORTHSTARCPP_RENDER_LIBRARY_LINKAGE_H_INCLUDED

#if NORTHSTARCPP_PLATFORM_WINDOWS
// #	include	<northstarcpp/render/dx9/library_linkage.h>
// #	include	<northstarcpp/render/dx10/library_linkage.h>
#elif NORTHSTARCPP_PLATFORM_XBOX_360	// #if NORTHSTARCPP_PLATFORM_WINDOWS
#	include	<northstarcpp/render/xbox360/library_linkage.h>
#elif NORTHSTARCPP_PLATFORM_PS3			// #elif NORTHSTARCPP_PLATFORM_XBOX_360
#	include	<northstarcpp/render/ps3/library_linkage.h>
#endif // #elif NORTHSTARCPP_PLATFORM_PS3

#endif // #ifndef NORTHSTARCPP_RENDER_LIBRARY_LINKAGE_H_INCLUDED