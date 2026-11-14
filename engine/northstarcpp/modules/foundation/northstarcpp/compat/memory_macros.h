////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_MACROS_H_INCLUDED
#define NORTHSTARCPP_MEMORY_MACROS_H_INCLUDED

#ifdef DEBUG

#	define NORTHSTARCPP_NEW_IMPL( allocator, type )								\
			new ( northstarcpp::memory::new_helper<type>::call( strip_pointer(allocator), __FUNCTION__, __FILE__, __LINE__ ) ) ( type )

#	define NORTHSTARCPP_NEW_ARRAY_IMPL( allocator, type, count )				\
			northstarcpp::memory::new_array_helper<type>::call( strip_pointer(allocator), count, __FUNCTION__, __FILE__, __LINE__ )

#	define NORTHSTARCPP_DELETE_IMPL( allocator, pointer, ... )					\
			northstarcpp::memory::delete_helper			( strip_pointer(allocator), pointer, __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__ )

#	define NORTHSTARCPP_DELETE_ARRAY_IMPL( allocator, pointer, ... )			\
			northstarcpp::memory::delete_array_helper	( strip_pointer(allocator), pointer, __FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__ )

#	define NORTHSTARCPP_MALLOC_IMPL( allocator, size, description )				\
			northstarcpp::memory::malloc_helper			( strip_pointer(allocator), size, description, __FUNCTION__, __FILE__, __LINE__ )

#	define NORTHSTARCPP_REALLOC_IMPL( allocator, pointer, size, description )	\
			northstarcpp::memory::realloc_helper		( strip_pointer(allocator), pointer, size, description, __FUNCTION__, __FILE__, __LINE__ )

#	define NORTHSTARCPP_FREE_IMPL( allocator, pointer )							\
			northstarcpp::memory::free_helper			( strip_pointer(allocator), pointer, __FUNCTION__, __FILE__, __LINE__ )

#else // #ifdef DEBUG

#	define NORTHSTARCPP_NEW_IMPL( allocator, type )								\
			new ( northstarcpp::memory::new_helper<type>::call( strip_pointer(allocator) ) ) ( type ) 

#	define NORTHSTARCPP_NEW_ARRAY_IMPL( allocator, type, count )				\
			northstarcpp::memory::new_array_helper<type>::call( strip_pointer(allocator), count )

#	define NORTHSTARCPP_DELETE_IMPL( allocator, pointer, ... )					\
			northstarcpp::memory::delete_helper			( strip_pointer(allocator), pointer, ##__VA_ARGS__ )

#	define NORTHSTARCPP_DELETE_ARRAY_IMPL( allocator, pointer, ... )			\
			northstarcpp::memory::delete_array_helper	( strip_pointer(allocator), pointer, ##__VA_ARGS__ )

#	define NORTHSTARCPP_MALLOC_IMPL( allocator, size, description )				\
			northstarcpp::memory::malloc_helper			( strip_pointer(allocator), size )

#	define NORTHSTARCPP_REALLOC_IMPL( allocator, pointer, size, description )	\
			northstarcpp::memory::realloc_helper		( strip_pointer(allocator), pointer, size )

#	define NORTHSTARCPP_FREE_IMPL( allocator, pointer )							\
			northstarcpp::memory::free_helper			( strip_pointer(allocator), pointer )

#endif // #ifdef DEBUG

#define NORTHSTARCPP_ALLOC_IMPL( allocator, type, count )						\
			( ( type* )NORTHSTARCPP_MALLOC_IMPL( allocator, sizeof( type )*( count ), typeid( type ).name( ) ) )

#endif // #ifndef NORTHSTARCPP_MEMORY_MACROS_H_INCLUDED