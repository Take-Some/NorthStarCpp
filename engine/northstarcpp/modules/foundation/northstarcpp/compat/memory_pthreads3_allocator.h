////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_PTHREADS3_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_MEMORY_PTHREADS3_ALLOCATOR_H_INCLUDED

#include <northstarcpp/compat/memory_base_allocator.h>
#include <northstarcpp/compat/memory_debug_parameters.h>

namespace northstarcpp {
namespace memory {

#if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR
class NORTHSTARCPP_CORE_API pthreads3_allocator : public base_allocator {
public:
			pvoid	malloc_impl			( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			pvoid	realloc_impl		( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			void	free_impl			( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );

private:
	typedef	base_allocator				super;

private:
	virtual	pvoid	call_malloc			( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	pvoid	call_realloc		( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	void	call_free			( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	void	initialize_impl		( pvoid arena, u64 arena_size, pcstr arena_id );
	virtual	void	finalize_impl		( );
	virtual	size_t	total_size			( ) const;
	virtual	size_t	allocated_size		( ) const;
}; // class pthreads3_allocator

#else // #if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR

typedef crt_allocator pthreads3_allocator;

#endif // #if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_PTHREADS3_ALLOCATOR_H_INCLUDED