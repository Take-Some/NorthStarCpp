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

#include <northstarcpp/macro_extensions.h>
#include <northstarcpp/optimization_extensions.h>
#include <northstarcpp/type_extensions.h>
#include <northstarcpp/platform_extensions.h>
#include <northstarcpp/detail_noncopyable.h>
#include <northstarcpp/debug_extensions.h>
#include <northstarcpp/stdlib_extensions.h>
#include <northstarcpp/memory_extensions.h>
#include <northstarcpp/debug_static_cast_checked.h>
#include <northstarcpp/debug_pointer_cast.h>
#include <northstarcpp/command_line_extensions.h>
#include <northstarcpp/os_extensions.h>
#include <northstarcpp/timing_extensions.h>
#include <northstarcpp/threading_extensions.h>
#include <northstarcpp/strings_extensions.h>
#include <northstarcpp/debug_check_no_multithread.h>
#include <northstarcpp/logging_extensions.h>
#include <northstarcpp/pointer_extensions.h>
#include <northstarcpp/debug_pointers.h>
#include <northstarcpp/containers_extensions.h>
#include <northstarcpp/math_extensions.h>
#include <northstarcpp/memory_chunk_reader.h>
#include <northstarcpp/memory_writer.h>
#include <northstarcpp/tasks_system.h>
#include <northstarcpp/configs.h>

#if !NORTHSTARCPP_CORE_BUILDING
	namespace northstarcpp {
		using math::float2;
		using math::float3;
		using math::float4;
		using math::float4x4;
	} // namespace northstarcpp
#endif // #if !NORTHSTARCPP_CORE_BUILDING

#endif // #ifndef NORTHSTARCPP_EXTENSIONS_H_INCLUDED