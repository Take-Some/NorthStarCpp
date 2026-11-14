////////////////////////////////////////////////////////////////////////////
//	Created 	: 11.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <northstarcpp/compat/buffer_vector.h>
#include <northstarcpp/compat/fixed_vector.h>
#include <northstarcpp/compat/associative_vector.h>
#include <northstarcpp/compat/hash_multiset.h>

#include <map>
#include <vector>
#include <set>

#include <northstarcpp/compat/network/api.h>
#include "std_string.h"

namespace northstarcpp {
namespace network {

extern allocator_type*						g_allocator;

#define USER_ALLOCATOR						*::northstarcpp::network::g_allocator
#include <northstarcpp/compat/std_containers.h>
#undef USER_ALLOCATOR

template < typename T >
class vector_size_t : public vector< T > {
private:
	typedef vector< T >						super;

public:
	typedef vector_size_t< T >				self_type;
	typedef typename super::size_type		size_type;
	typedef typename super::value_type		value_type;
	typedef typename super::reference		reference;
	typedef typename super::const_reference	const_reference;

public:
	inline				vector_size_t		( ) {}
	inline				vector_size_t		( size_type count, value_type const& value ) : super(count,value) {}
	inline				vector_size_t		( self_type const& other) : super(other) {}
	inline	explicit	vector_size_t		( size_type count) : super(count) {}
	template < typename input_iterator >
	inline				vector_size_t		( input_iterator const& first, input_iterator const& last ) : super(first, last) {}
	inline	reference		operator[]		( size_t index ) { return super::operator[]( (u32)index ); }
	inline	const_reference	operator[]		( size_t index ) const { return super::operator[]( (u32)index ); }
}; // class vector

} // namespace network
} // namespace northstarcpp

#define XN_NEW( type )								NORTHSTARCPP_NEW_IMPL(		*::northstarcpp::network::g_allocator, type )
#define XN_DELETE( pointer )						NORTHSTARCPP_DELETE_IMPL(	*::northstarcpp::network::g_allocator, pointer )
#define XN_MALLOC( size, description )				NORTHSTARCPP_MALLOC_IMPL(	*::northstarcpp::network::g_allocator, size, description )
#define XN_REALLOC( pointer, size, description )	NORTHSTARCPP_REALLOC_IMPL(	*::northstarcpp::network::g_allocator, pointer, size, description )
#define XN_FREE( pointer )							NORTHSTARCPP_FREE_IMPL(		*::northstarcpp::network::g_allocator, pointer )
#define XN_ALLOC( type, count )						NORTHSTARCPP_ALLOC_IMPL(	*::northstarcpp::network::g_allocator, type, count )

#define NORTHSTARCPP_BOOST_NEW								XN_NEW
#define NORTHSTARCPP_BOOST_DELETE							XN_DELETE

#define NORTHSTARCPP_BOOST_MALLOC							XN_MALLOC
#define NORTHSTARCPP_BOOST_FREE								XN_FREE

#endif // #ifndef MEMORY_H_INCLUDED