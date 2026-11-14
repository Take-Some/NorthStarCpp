////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.01.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "memory_monitor.h"
#include <new.h>
#include "memory_process_allocator.h"

using northstarcpp::memory::process_allocator;

void process_allocator::initialize_impl	( pvoid buffer, u64 buffer_size, pcstr arena_id )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS( buffer, buffer_size, arena_id );
}

void process_allocator::finalize_impl	( )
{
}

pvoid process_allocator::malloc_impl	( size_t const size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER			( size );
	NORTHSTARCPP_CORE_DEBUG_PARAMETERS_UNREFERENCED_GUARD;

	ASSERT							( initialized ( ) );

	pvoid const result				= 0;

	return							
		on_malloc(
			result,
			size,
			0,
			NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_PARAMETER
		);
}

pvoid process_allocator::realloc_impl	( pvoid pointer, size_t const new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	ASSERT							( initialized ( ) );
	if ( !new_size ) {
		free_impl					( pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
		return						( 0 );
	}

	size_t const previous_size		= pointer ? buffer_size( pointer ) : 0;
	if ( pointer )
		on_free						( pointer, false );

	pvoid const result				= 0;

	return							
		on_malloc					(
			result, 
			new_size, 
			previous_size, 
			NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_PARAMETER
		);
}

void process_allocator::free_impl		( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	NORTHSTARCPP_CORE_DEBUG_PARAMETERS_UNREFERENCED_GUARD;

	ASSERT							( initialized ( ) );

	if ( !pointer )
		return;

	on_free							( pointer );
}

pvoid process_allocator::call_malloc	( size_t size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return							( malloc_impl( size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION NORTHSTARCPP_CORE_DEBUG_PARAMETERS ) );
}

pvoid process_allocator::call_realloc	( pvoid pointer, size_t new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION_DECLARATION NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return							( realloc_impl( pointer, new_size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DESCRIPTION NORTHSTARCPP_CORE_DEBUG_PARAMETERS ) );
}

void process_allocator::call_free		( pvoid pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	free_impl						( pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}