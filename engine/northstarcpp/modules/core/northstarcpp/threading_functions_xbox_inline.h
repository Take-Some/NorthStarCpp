////////////////////////////////////////////////////////////////////////////
//	Created		: 11.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef THREADING_FUNCTIONS_XBOX_INLINE_H_INCLUDED
#define THREADING_FUNCTIONS_XBOX_INLINE_H_INCLUDED

namespace northstarcpp {
namespace threading {

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_increment					( atomic32_type& value )
{
	return				InterlockedIncrement( &value );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_decrement					( atomic32_type& value )
{
	return				InterlockedDecrement( &value );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_exchange_add				( atomic32_type& value, atomic32_value_type increment )
{
	return				InterlockedExchangeAdd( &value, increment );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type	interlocked_exchange_sub			( atomic32_type& value,  atomic32_value_type  subtract )
{
	return				InterlockedExchangeAdd( &value, -subtract );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_and						( atomic32_type& value, atomic32_value_type mask )
{
	return				InterlockedAnd( &value, mask );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_or						( atomic32_type& value, atomic32_value_type mask )
{
	return				InterlockedOr( &value, mask );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_exchange					( atomic32_type& target, atomic32_value_type value )
{
	return				InterlockedExchange( &target, value );
}

NORTHSTARCPP_THREADING_INLINE atomic_ptr_value_type interlocked_exchange_pointer		( atomic_ptr_type& target, atomic_ptr_value_type const value )
{
	return				InterlockedExchangePointer( &target, value );
}

NORTHSTARCPP_THREADING_INLINE atomic32_value_type interlocked_compare_exchange			( atomic32_type& target, atomic32_value_type exchange, atomic32_value_type comparand )
{
	return				InterlockedCompareExchange( &target, exchange, comparand);
}

NORTHSTARCPP_THREADING_INLINE atomic64_value_type interlocked_compare_exchange			( atomic64_type& target, atomic64_value_type exchange, atomic64_value_type comparand )
{
	return				InterlockedCompareExchange64((__int64*)&target, exchange, comparand);
}

NORTHSTARCPP_THREADING_INLINE atomic_ptr_value_type interlocked_compare_exchange_pointer( atomic_ptr_type& target, atomic_ptr_value_type const exchange, atomic_ptr_value_type const comparand )
{
	return				InterlockedCompareExchangePointer(& target, exchange, comparand);
}

} // namespace threading
} // namespace northstarcpp

#endif // #ifndef THREADING_FUNCTIONS_XBOX_INLINE_H_INCLUDED