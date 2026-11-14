////////////////////////////////////////////////////////////////////////////
//	Created		: 11.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef THREADING_FUNCTIONS_WIN_XBOX_INLINE_H_INCLUDED
#define THREADING_FUNCTIONS_WIN_XBOX_INLINE_H_INCLUDED

#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange,		\
				  _InterlockedCompareExchange64,	\
				  _InterlockedExchange,				\
				  _InterlockedIncrement,			\
				  _InterlockedDecrement,			\
				  _InterlockedAnd,					\
				  _InterlockedOr,					\
				  _InterlockedXor,					\
				  _InterlockedExchangeAdd			\
				 )
#if NORTHSTARCPP_PLATFORM_WINDOWS_64
#	pragma intrinsic(_InterlockedExchangePointer)
#	define NORTHSTARCPP_INTERLOCKED_EXCHANGE_POINTER(a,b)			_InterlockedExchangePointer(a,b)
#	define NORTHSTARCPP_INTERLOCKED_COMPARE_EXCHANGE_POINTER(a,b,c)	_InterlockedCompareExchangePointer(&a,b,c)
#else // #if NORTHSTARCPP_PLATFORM_WINDOWS_64
#	define NORTHSTARCPP_INTERLOCKED_EXCHANGE_POINTER(a,b)			(atomic_ptr_value_type)_InterlockedExchange( (atomic32_type*)(a), *(atomic32_value_type const*)(&b) )
#	define NORTHSTARCPP_INTERLOCKED_COMPARE_EXCHANGE_POINTER(a,b,c)	(atomic_ptr_value_type)_InterlockedCompareExchange( ( atomic32_type* )&a, *(atomic32_value_type const*)&b, *(atomic32_value_type const*)&c )
#endif // #if NORTHSTARCPP_PLATFORM_WINDOWS_64

namespace northstarcpp {
namespace threading {

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_increment					( atomic32_type& value )
{
	return				( _InterlockedIncrement( &value ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_decrement					( atomic32_type& value )
{
	return				( _InterlockedDecrement( &value ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_exchange_add				( atomic32_type& value, atomic32_value_type increment )
{
	return				( _InterlockedExchangeAdd( &value, increment ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type	interlocked_exchange_sub			( atomic32_type& value,  atomic32_value_type  subtract )
{
	return				( _InterlockedExchangeAdd( &value, -subtract ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_and						( atomic32_type& value, atomic32_value_type mask )
{
	return				( _InterlockedAnd( &value, mask ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_or						( atomic32_type& value, atomic32_value_type mask )
{
	return				( _InterlockedOr( &value, mask ) );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_exchange					( atomic32_type& target, atomic32_value_type value )
{
	return				_InterlockedExchange( &target, value );
}

NORTHSTARCPP_THREADING_INLINE atomic_ptr_value_type interlocked_exchange_pointer		( atomic_ptr_type& target, atomic_ptr_value_type const value )
{
	return				NORTHSTARCPP_INTERLOCKED_EXCHANGE_POINTER( &target, value );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_compare_exchange			( atomic32_type& target, atomic32_value_type exchange, atomic32_value_type comperand )
{
	return				_InterlockedCompareExchange( &target, exchange, comperand);
}

NORTHSTARCPP_THREADING_INLINE atomic64_value_type interlocked_compare_exchange			( atomic64_type& target, atomic64_value_type exchange, atomic64_value_type comperand )
{
	return				_InterlockedCompareExchange64(&target, exchange, comperand);
}

NORTHSTARCPP_THREADING_INLINE atomic_ptr_value_type interlocked_compare_exchange_pointer( atomic_ptr_type& target, atomic_ptr_value_type const exchange, atomic_ptr_value_type const comperand )
{
	return				NORTHSTARCPP_INTERLOCKED_COMPARE_EXCHANGE_POINTER(target, exchange, comperand);
}

} // namespace threading
} // namespace northstarcpp

#endif // #ifndef THREADING_FUNCTIONS_WIN_XBOX_INLINE_H_INCLUDED
