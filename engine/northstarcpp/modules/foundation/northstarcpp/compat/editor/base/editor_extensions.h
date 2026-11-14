//-------------------------------------------------------------------------------------------
//	Created		: 26.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------

#ifndef EDITOR_EXTENSIONS_H_INCLUDED
#define EDITOR_EXTENSIONS_H_INCLUDED

namespace northstarcpp {

	template <typename T>
	inline T const & identity( T const & value ) { return value; }

	template <typename T>
	inline T const * identity( T const * value ) { return value; }

	inline bool identity( System::Object^ value ) { return !!&value; }

} // namespace northstarcpp

#pragma managed(push,off)
#	include <northstarcpp/compat/extensions.h>
#pragma managed(pop)

#include <northstarcpp/compat/os_preinclude.h>
#undef	NOGDICAPMASKS     			// CC_*, LC_*, PC_*, CP_*, TC_*, RC_
#undef	NOVIRTUALKEYCODES 			// VK_*
#undef	NOWINMESSAGES     			// WM_*, EM_*, LB_*, CB_*
#undef	NOWINSTYLES       			// WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
#undef	NOSYSMETRICS      			// SM_*
#undef	NOMENUS           			// MF_*
#undef	NOICONS           			// IDI_*
#undef	NOKEYSTATES       			// MK_*
#undef	NOSYSCOMMANDS     			// SC_*
//#undef	NORASTEROPS       			// Binary and Tertiary raster ops
#undef	NOSHOWWINDOW      			// SW_*
//#undef	OEMRESOURCE       			// OEM Resource values
#undef	NOATOM            			// Atom Manager routines
#undef	NOCLIPBOARD       			// Clipboard routines
#undef	NOCOLOR           			// Screen colors
#undef	NOCTLMGR          			// Control and Dialog routines
#undef	NODRAWTEXT        			// DrawText() and DT_*
#undef	NOGDI             			// All GDI defines and routines
#undef	NOKERNEL          			// All KERNEL defines and routines
#undef	NOUSER            			// All USER defines and routines
#undef	NONLS             			// All NLS defines and routines
#undef	NOMB              			// MB_* and MessageBox()
#undef	NOMEMMGR          			// GMEM_*, LMEM_*, GHND, LHND, associated routines
#undef	NOMETAFILE        			// typedef METAFILEPICT
//#undef	NOMINMAX          			// Macros min(a,b) and max(a,b)
#undef	NOMSG             			// typedef MSG and associated routines
//#undef	NOOPENFILE        			// OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#undef	NOSCROLL          			// SB_* and scrolling routines
#undef	NOSERVICE         			// All Service Controller routines, SERVICE_ equates, etc.
#undef	NOSOUND           			// Sound driver routines
#undef	NOTEXTMETRIC      			// typedef TEXTMETRIC and associated routines
#undef	NOWH              			// SetWindowsHook and WH_*
#undef	NOWINOFFSETS      			// GWL_*, GCL_*, associated routines
//#undef	NOCOMM            			// COMM driver routines
//#undef	NOKANJI           			// Kanji support stuff.
//#undef	NOHELP            			// Help engine interface.
//#undef	NOPROFILER        			// Profiler interface.
//#undef	NODEFERWINDOWPOS  			// DeferWindowPos routines
//#undef	NOMCX             			// Modem Configuration Extensions
#include <northstarcpp/compat/os_include.h>

#include <stdio.h>
#include <stdlib.h>
#include <vcclr.h>

#pragma managed(push,off)
#include <northstarcpp/compat/resources_fs.h>
#include <northstarcpp/compat/configs.h>
#pragma managed(pop)

using northstarcpp::math::float4x4;
using northstarcpp::math::float3;
using northstarcpp::math::float4;
using northstarcpp::math::aabb;
using northstarcpp::math::color;

#include "converting.h"

#endif // #ifndef EDITOR_EXTENSIONS_H_INCLUDED