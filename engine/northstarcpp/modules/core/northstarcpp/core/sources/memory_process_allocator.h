////////////////////////////////////////////////////////////////////////////
//	Created		: 30.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_PROCESS_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_MEMORY_PROCESS_ALLOCATOR_H_INCLUDED

#include <northstarcpp/memory_base_allocator.h>
#include <northstarcpp/memory_debug_parameters.h>

namespace northstarcpp {
namespace memory {

class NORTHSTARCPP_CORE_API process_allocator : public base_allocator {
public:
			pvoid	malloc_impl			( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			pvoid	realloc_impl		( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			void	free_impl			( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	size_t	allocated_size		( ) const;

private:
	typedef	base_allocator				super;

protected:
	virtual	pvoid	call_malloc			( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	pvoid	call_realloc		( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
	virtual	void	call_free			( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );

private:
	virtual	void	initialize_impl		( pvoid arena, u64 arena_size, pcstr arena_id );
	virtual	void	finalize_impl		( );
	virtual	size_t	total_size			( ) const;
}; // class process_allocator

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_PROCESS_ALLOCATOR_H_INCLUDED