////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/compat/core/library_linkage.h>
#include <ogg/library_linkage.h>

#include <northstarcpp/compat/linkage_helper.h>

DECLARE_LINKAGE_ID(sound_library_linkage)

#if !NORTHSTARCPP_PLATFORM_PS3
#	pragma comment( lib, "x3daudio.lib" )
#endif // #if !NORTHSTARCPP_PLATFORM_PS3

#if NORTHSTARCPP_PLATFORM_XBOX_360
#	pragma comment( lib, "xaudio2.lib" )
#	pragma comment( lib, "xmcore.lib" )
#endif // #if NORTHSTARCPP_PLATFORM_XBOX_360