////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_EXTENSIONS_H_INCLUDED

#ifdef SN_TARGET_PS3
	namespace scestd = std;
	#include <assert.h>
	namespace std {
		void _SCE_Assert(const char *first, const char *second);
		void _Assert(const char *first, const char *second);
	} // namespace std
#endif // #ifdef SN_TARGET_PS3

#ifndef NORTHSTARCPP_DISABLE_NEW_OPERATOR
#	define NORTHSTARCPP_DISABLE_NEW_OPERATOR	1
#endif // #ifndef NORTHSTARCPP_DISABLE_NEW_OPERATOR

#define BOOST_NO_EXCEPTIONS   
#include <boost/noncopyable.hpp>

#include <northstarcpp/compat/macro_extensions.h>
#include <northstarcpp/compat/optimization_extensions.h>
#include <northstarcpp/compat/type_extensions.h>
#include <northstarcpp/compat/platform_extensions.h>
#include <northstarcpp/compat/detail_noncopyable.h>
#include <northstarcpp/compat/debug_extensions.h>
#include <northstarcpp/compat/stdlib_extensions.h>
#include <northstarcpp/compat/memory_extensions.h>
#include <northstarcpp/compat/debug_static_cast_checked.h>
#include <northstarcpp/compat/debug_pointer_cast.h>
#include <northstarcpp/compat/command_line_extensions.h>
#include <northstarcpp/compat/os_extensions.h>
#include <northstarcpp/compat/timing_extensions.h>
#include <northstarcpp/compat/threading_extensions.h>
#include <northstarcpp/compat/strings_extensions.h>
#include <northstarcpp/compat/debug_check_no_multithread.h>
#include <northstarcpp/compat/logging_extensions.h>
#include <northstarcpp/compat/pointer_extensions.h>
#include <northstarcpp/compat/debug_pointers.h>
#include <northstarcpp/compat/containers_extensions.h>
#include <northstarcpp/compat/math_extensions.h>
#include <northstarcpp/compat/memory_chunk_reader.h>
#include <northstarcpp/compat/memory_writer.h>
#include <northstarcpp/compat/tasks_system.h>
#include <northstarcpp/compat/configs.h>

#if !NORTHSTARCPP_CORE_BUILDING
	namespace northstarcpp {
		using math::float2;
		using math::float3;
		using math::float4;
		using math::float4x4;
	} // namespace northstarcpp
#endif // #if !NORTHSTARCPP_CORE_BUILDING

#endif // #ifndef NORTHSTARCPP_EXTENSIONS_H_INCLUDED