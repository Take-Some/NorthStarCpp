////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_CORE_BUILDING	1

#ifdef NORTHSTARCPP_STANDALONE_MATH
#	include <northstarcpp/core/math_standalone.h>
#else
#	define NORTHSTARCPP_IN_PCH	1
#	include <northstarcpp/extensions.h>
#	undef NORTHSTARCPP_IN_PCH
#endif

#endif // #ifndef PCH_H_INCLUDED