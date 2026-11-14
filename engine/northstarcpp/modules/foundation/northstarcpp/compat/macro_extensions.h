////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MACRO_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_MACRO_EXTENSIONS_H_INCLUDED

#if defined( _CPPUNWIND ) && !defined( _MANAGED )
#	error please disable exceptions
#endif // #if defined( _CPPUNWIND ) && !defined( _MANAGED )

#ifndef NDEBUG
#	define DEBUG
#endif // #ifdef NDEBUG

#include <northstarcpp/compat/macro_compile_assert.h>
#include <northstarcpp/compat/macro_strings.h>
#include <northstarcpp/compat/macro_platform.h>
#include <northstarcpp/compat/macro_compiler.h>
#include <northstarcpp/compat/macro_building.h>
#include <northstarcpp/compat/macro_unreferenced_parameter.h>
#include <northstarcpp/compat/macro_todo.h>

#define NORTHSTARCPP_ENGINE_COMPANY_ID	"2026 Kayla's Systems \u2014 Kayla Verner"
#define NORTHSTARCPP_ENGINE_VERSION		2
#define NORTHSTARCPP_ENGINE_SUBVERSION	0
#define NORTHSTARCPP_ENGINE_ID			"NorthStarCpp"

#define IMPLICIT

#endif // #ifndef NORTHSTARCPP_MACRO_EXTENSIONS_H_INCLUDED