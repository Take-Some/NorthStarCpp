////////////////////////////////////////////////////////////////////////////
//	Created 	: 18.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_ARRAY_CHUNK_READER_H_INCLUDED
#define NORTHSTARCPP_MEMORY_ARRAY_CHUNK_READER_H_INCLUDED

namespace northstarcpp {
namespace memory {

template < typename implementation >
class array_chunk_reader {
public:
	inline	void	construct		( );
	inline	u32		chunk_position	( u32 chunk_id );

private:
	inline	implementation&	impl	( );
}; // class reader

} // namespace memory
} // namespace northstarcpp

#include <northstarcpp/compat/memory_array_chunk_reader_inline.h>

#endif // #ifndef NORTHSTARCPP_MEMORY_ARRAY_CHUNK_READER_H_INCLUDED