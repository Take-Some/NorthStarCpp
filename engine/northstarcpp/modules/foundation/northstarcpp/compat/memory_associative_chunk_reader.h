////////////////////////////////////////////////////////////////////////////
//	Created 	: 18.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_ASSOCIATIVE_CHUNK_READER_H_INCLUDED
#define NORTHSTARCPP_MEMORY_ASSOCIATIVE_CHUNK_READER_H_INCLUDED

namespace northstarcpp {
namespace memory {

template < typename implementation >
class associative_chunk_reader {
public:
	inline	void	construct		( );
	inline	u32		chunk_position	( u32 chunk_id );

private:
	typedef std::pair<u32, u32>		Pair;

	struct predicate {
		u32	m_chunk_id;

		inline		predicate		( u32 const chunk_id ) :
			m_chunk_id	( chunk_id )
		{
		}

		inline bool	operator( )		( Pair const& value ) const
		{
			return	( m_chunk_id == value.first );
		}
	}; // struct predicate

private:
	inline	implementation&	impl	( );
}; // class reader

} // namespace memory
} // namespace northstarcpp

#include <northstarcpp/compat/memory_associative_chunk_reader_inline.h>

#endif // #ifndef NORTHSTARCPP_MEMORY_ASSOCIATIVE_CHUNK_READER_H_INCLUDED