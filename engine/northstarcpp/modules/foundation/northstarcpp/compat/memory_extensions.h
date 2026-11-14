////////////////////////////////////////////////////////////////////////////
//	Created 	: 02.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_MEMORY_EXTENSIONS_H_INCLUDED

#include <northstarcpp/compat/macro_extensions.h>
#include <northstarcpp/compat/type_extensions.h>
#include <northstarcpp/compat/debug_extensions.h>

#if NORTHSTARCPP_PLATFORM_WINDOWS
#	define NORTHSTARCPP_MAX_CACHE_LINE_SIZE		64
#elif NORTHSTARCPP_PLATFORM_XBOX_360 // #if NORTHSTARCPP_PLATFORM_WINDOWS
#	define NORTHSTARCPP_MAX_CACHE_LINE_SIZE		128
#elif NORTHSTARCPP_PLATFORM_PS3 // #elif NORTHSTARCPP_PLATFORM_XBOX_360
#	define NORTHSTARCPP_MAX_CACHE_LINE_SIZE		128
#else // #elif NORTHSTARCPP_PLATFORM_PS3
#	error please define your platform!
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS

#define NORTHSTARCPP_MAX_CACHE_LINE_PAD			char NORTHSTARCPP_STRING_CONCAT(m_cache_line_pad_$, __LINE__) [NORTHSTARCPP_MAX_CACHE_LINE_SIZE]

#define NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR	0

#if NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR || defined(NORTHSTARCPP_MAYA_BUILDING) || defined(__SNC__)
#	include <northstarcpp/compat/memory_crt_allocator.h>
#endif // #if NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR

#define NORTHSTARCPP_DEFAULT_ALIGN_SIZE			8
#define NORTHSTARCPP_DEFAULT_ALIGN				NORTHSTARCPP_ALIGN(NORTHSTARCPP_DEFAULT_ALIGN_SIZE)

#include <northstarcpp/compat/memory_pthreads3_allocator.h>

namespace northstarcpp {
namespace memory {

typedef pthreads3_allocator						pthreads_allocator_type;
extern NORTHSTARCPP_CORE_API pthreads_allocator_type	g_mt_allocator;

} // namespace memory
} // namespace northstarcpp

#include <northstarcpp/compat/memory_override_operators.h>
#include <northstarcpp/compat/memory_doug_lea_allocator.h>
#include <northstarcpp/compat/memory_allocator_helper.h>
#include <northstarcpp/compat/memory_macros.h>
#include <northstarcpp/compat/memory_buffer.h>
#include <northstarcpp/compat/uninitialized_reference.h>
#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {

enum {
	Kb	= u32(1024),
	Mb	= 1024*Kb,
};

namespace memory {

namespace debug {
	enum {
		underrun_guard			= 0xffbdFDFD,
		overrun_guard			= 0xffbdFDFD,
		uninitialized_memory	= 0xffbdCDCD,
		freed_memory			= 0xffbdDDDD,
	};
} // namespace debug

NORTHSTARCPP_CORE_API	void allocate_region				( );

typedef doug_lea_allocator							doug_lea_allocator_type;
typedef pthreads3_allocator							pthreads_allocator_type;

extern NORTHSTARCPP_CORE_API pthreads_allocator_type		g_mt_allocator;

typedef fastdelegate::FastDelegate<void ( base_allocator const&, u32, pvoid, u64, pcstr )>	allocator_predicate_type;
NORTHSTARCPP_CORE_API	void iterate_allocators				( allocator_predicate_type const& predicate );
NORTHSTARCPP_CORE_API	void register_allocator				( base_allocator& allocator, u64 arena_size, pcstr description );

NORTHSTARCPP_CORE_API	void dump_statistics				( );
NORTHSTARCPP_CORE_API	void copy							( mutable_buffer const & destination, const_buffer const & source );
NORTHSTARCPP_CORE_API	void copy							( pvoid destination, size_t destination_size, pcvoid source, size_t source_size );

NORTHSTARCPP_CORE_API	void zero							( mutable_buffer const & destination );
NORTHSTARCPP_CORE_API	void zero							( pvoid destination, size_t destination_size );

				template <class T, size_t count>
				void zero							( T (& destination)[count] ) { zero(destination, count * sizeof(T)); }

NORTHSTARCPP_CORE_API	void fill32							( mutable_buffer const & destination, u32 value, u32 count );
NORTHSTARCPP_CORE_API	void fill32							( pvoid destination, size_t destination_size_in_bytes, u32 value, size_t count );
NORTHSTARCPP_CORE_API	void fill8							( mutable_buffer const & destination, u8  value, u32 count );
NORTHSTARCPP_CORE_API	void fill8							( pvoid destination, size_t destination_size_in_bytes, u8  value, size_t count );

NORTHSTARCPP_CORE_API	int  compare						( const_buffer const & buffer1, const_buffer const & buffer2 );
NORTHSTARCPP_CORE_API	bool equal							( const_buffer const & buffer1, const_buffer const & buffer2 );

NORTHSTARCPP_CORE_API	void lock_process_heap				( );
NORTHSTARCPP_CORE_API	bool try_lock_process_heap			( );
NORTHSTARCPP_CORE_API	void unlock_process_heap			( );

} // namespace memory
} // namespace northstarcpp

// multithreaded general purpose memory allocation macros
// use ONLY in case when you need multi threaded functionality
// otherwise use specific single threaded memory allocator
#define MT_NEW( type )								NORTHSTARCPP_NEW_IMPL(		::northstarcpp::memory::g_mt_allocator, type )
#define MT_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	::northstarcpp::memory::g_mt_allocator, pointer )
#define MT_MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	::northstarcpp::memory::g_mt_allocator, size, description )
#define MT_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	::northstarcpp::memory::g_mt_allocator, pointer, size, description )
#define MT_FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		::northstarcpp::memory::g_mt_allocator, pointer )
#define MT_ALLOC( type, count )						NORTHSTARCPP_ALLOC_IMPL(	::northstarcpp::memory::g_mt_allocator, type, count )

#endif // #ifndef NORTHSTARCPP_MEMORY_EXTENSIONS_H_INCLUDED