////////////////////////////////////////////////////////////////////////////
//	Created 	: 30.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_ALLOCATOR_HELPER_INLINE_H_INCLUDED
#define NORTHSTARCPP_ALLOCATOR_HELPER_INLINE_H_INCLUDED

#include <boost/type_traits/is_pod.hpp>

namespace northstarcpp {
namespace memory {

//-----------------------------------------------------------------------------------
// detail
//-----------------------------------------------------------------------------------
namespace detail {

template < typename T, bool >
struct get_top_pointer_helper 
{
	static pvoid   call			( T*& pointer )
	{
		return					( pointer );
	}
}; // get_top_pointer_helper 

template < typename T >
struct get_top_pointer_helper<T, true>
{
	static pvoid   call			( T*& pointer )
	{
		return					( dynamic_cast<void*>(pointer) );
	}
}; // get_top_pointer_helper 

struct call_destructor_predicate
{
	template <typename T>
	void operator( )			( T* const pointer ) const
	{
		NORTHSTARCPP_UNREFERENCED_PARAMETER	( pointer );
		pointer->~T				( );
	}

}; // struct call_destructor_helper

template <class T, bool is_pod>
struct call_constructor_helper
{
	static void		call			( T* const begin, T* const end )
	{
		T* i				= begin;
		for ( ; i != end; ++i )
			new ( i ) T		( );
	}
}; // struct call_constructor_helper

template <class T>
struct call_constructor_helper<T, true>
{
	static void		call			( T* const begin, T* const end )
	{
		NORTHSTARCPP_UNREFERENCED_PARAMETERS	( begin, end );
	}

}; // struct call_constructor_helper

template <class T>
void   call_constructor		( T* const begin, T* const end )
{
#ifdef _MSC_VER
	call_constructor_helper<T, __is_pod(T) >::call 	(begin, end);
#else // #ifdef _MSC_VER
	call_constructor_helper<T, boost::is_pod<T>::value >::call 	(begin, end);
#endif // #ifdef _MSC_VER
}

template < typename T >
pvoid   get_top_pointer ( T*& pointer );

template <typename A, typename T, typename P>
void	  delete_array_helper_impl	( A& allocator, 
									  T*& pointer 
									  NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION, 
									  P const& call_destructor_predicate )
{
	pvoid 	  real_pointer		= get_top_pointer<T>(pointer);
	u32*	  result			= ( ( u32* )real_pointer ) - 2;
	u32 const count				= result[0];
	ASSERT						( count < ( u32(1) << 31 ) );

	u32 const size				= result[1];

	pbyte i						= ( pbyte )pointer;
	pbyte e						= i + size*count;
	for ( ; i != e; i += size )
		call_destructor_predicate	( ( T* )i );

	allocator.free_impl			( result NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}

template <typename A, typename T>
void   free_helper_impl	( A& allocator, T*& pointer, pvoid const top_pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	allocator.free_impl			( top_pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
	pointer						= 0;
}

template < typename T >
pvoid   get_top_pointer ( T*& pointer )
{
	return	get_top_pointer_helper<T,boost::is_polymorphic< T >::value >::call (pointer);
}

template <typename A, typename T, typename P>
void   delete_helper_impl		( A& allocator, 
								  T*& pointer 
								  NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION, 
								  P const& call_destructor_predicate )
{
	if ( !pointer )
		return;

	pvoid top_pointer			= detail::get_top_pointer( pointer );
	call_destructor_predicate	( pointer );
	detail::free_helper_impl	( allocator, pointer, top_pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}

} // namespace detail

//-----------------------------------------------------------------------------------
// allocations macro helpers
//-----------------------------------------------------------------------------------
#ifdef _MANAGED
#	pragma managed (push, on)
template < typename A, typename T >
void   free_helper				( A& allocator, T* pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	if ( !pointer )
		return;

	detail::free_helper_impl	( allocator, pointer, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}
#	pragma managed (pop)
#else // #ifdef _MANAGED

template < typename A, typename T >
void   free_helper				( A& allocator, T*& pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	if ( !pointer )
		return;

	detail::free_helper_impl	( allocator, pointer, (typename boost::remove_cv<T>::type *)pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}
#endif // #ifdef _MANAGED

template <typename T>
template <typename A>
pvoid   new_helper<T>::call		( A& allocator NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return						( allocator.malloc_impl( sizeof(T) 
													  NORTHSTARCPP_CORE_DEBUG_PARAMETERS_TYPEID(T) 
													  NORTHSTARCPP_CORE_DEBUG_PARAMETERS ) );
}


template <typename T>
template <typename A>
T*   new_array_helper<T>::call	( A& allocator, u32 const count NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	u32*		result			= ( u32* )allocator.malloc_impl(
									2*sizeof( u32 ) + count*sizeof(T) 
									NORTHSTARCPP_CORE_DEBUG_PARAMETERS_TYPEID(T) NORTHSTARCPP_CORE_DEBUG_PARAMETERS );

	*result++					= count;
	*result++					= sizeof( T );
	T* const b					= ( T* )result;
	detail::call_constructor	( b, b + count );
	return						( b );
}

template < typename A, typename T >
void delete_array_helper	( A& allocator, T*& pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	detail::delete_array_helper_impl	( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, detail::call_destructor_predicate( ) );
}

template < typename A, typename T, typename P >
void   delete_array_helper	( A& allocator, T*& pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION, P const& delete_predicate)
{
	detail::delete_array_helper_impl	( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, delete_predicate );
}

#ifdef _MANAGED
#	pragma managed (push, on)
	template < typename A, typename T >
	void delete_helper	( A& allocator, T* pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
	{
		detail::delete_helper_impl		( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, detail::call_destructor_predicate( ) );
	}
	template < typename A, typename T, typename P >
	void delete_helper		( A& allocator, T* pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION, P const& delete_predicate )
	{
		detail::delete_helper_impl		( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, delete_predicate );
	}

#	pragma managed (pop)
#else // #ifdef _MANAGED
	template < typename A, typename T >
	void delete_helper		( A& allocator, T*& pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
	{
		detail::delete_helper_impl	( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, detail::call_destructor_predicate() );
	}
	template < typename A, typename T, typename P >
	void delete_helper		( A& allocator, T*& pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION, P const& delete_predicate )
	{
		detail::delete_helper_impl	( allocator, pointer NORTHSTARCPP_CORE_DEBUG_PARAMETERS, delete_predicate );
	}
#endif // #ifdef _MANAGED

template <typename A>
pvoid   malloc_helper			( A& allocator, size_t const size, pcstr description NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return				allocator.malloc_impl( size, description NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}

template <typename A>
pvoid   malloc_helper			( A& allocator, size_t const size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return				allocator.malloc_impl( size NORTHSTARCPP_CORE_DEBUG_PARAMETERS );
}

template <typename A, typename T>
pvoid   realloc_helper			( A& allocator, T* pointer, size_t const size, pcstr description NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return				allocator.realloc_impl( pointer, size, description NORTHSTARCPP_CORE_DEBUG_PARAMETERS);
}

template <typename A, typename T>
pvoid   realloc_helper			( A& allocator, T* pointer, size_t const size NORTHSTARCPP_CORE_DEBUG_PARAMETERS_DECLARATION )
{
	return				allocator.realloc_impl( pointer, size NORTHSTARCPP_CORE_DEBUG_PARAMETERS);
}

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_ALLOCATOR_HELPER_INLINE_H_INCLUDED