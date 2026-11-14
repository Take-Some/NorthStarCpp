////////////////////////////////////////////////////////////////////////////
//	Created		: 01.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_CORE_RENDER_INCLUDE_H_INCLUDED
#define NORTHSTARCPP_RENDER_CORE_RENDER_INCLUDE_H_INCLUDED

#define USE_DX10						0

#include <northstarcpp/os_preinclude.h>
#undef NOUSER
#undef NOMSG
#undef NOGDI
#undef NOTEXTMETRIC
#include <northstarcpp/os_include.h>

#pragma warning(push)
#pragma warning(disable:4995)

#if ! USE_DX10 
#	include <D3D11.h>
#	include "D3DX11tex.h"
#	include	<d3dx9math.h>
#else

#	include <d3d10.h>
#	include <d3dx10.h>
// Fake. needed to compile without D3D11.h
	enum D3D11_PRIMITIVE {};

#endif



//#endif


#include <D3DCommon.h>
#include <D3DCompiler.h>
#pragma warning(pop)

#include <northstarcpp/render/core/d3d_defines.h>

#endif // #ifndef NORTHSTARCPP_RENDER_CORE_RENDER_INCLUDE_H_INCLUDED