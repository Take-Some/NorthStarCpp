////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_DOUG_LEA_ALLOCATOR_H_INCLUDED
#define NORTHSTARCPP_MEMORY_DOUG_LEA_ALLOCATOR_H_INCLUDED

#include <northstarcpp/compat/memory_base_allocator.h>
#include <northstarcpp/compat/memory_debug_parameters.h>

namespace northstarcpp {
namespace memory {

#if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR
class NORTHSTARCPP_CORE_API doug_lea_allocator : public base_allocator {
public:
					doug_lea_allocator	( );
			void	user_thread_id		( u32 user_thread_id ) const;
			void	user_current_thread_id	( ) const;
			pvoid	malloc_impl			( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			pvoid	realloc_impl		( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );
			void	free_impl			( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION );

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
	virtual	size_t	allocated_size		( ) const;
	inline	bool	initialized			( ) const;

public:
	pvoid			m_arena;
	mutable u32		m_user_thread_id;
}; // class doug_lea_allocator

#else // #if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR

typedef crt_allocator doug_lea_allocator;

#endif // #if !NORTHSTARCPP_USE_CRT_MEMORY_ALLOCATOR

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_DOUG_LEA_ALLOCATOR_H_INCLUDED