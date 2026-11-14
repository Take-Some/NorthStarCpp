////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_CHUNK_READER_H_INCLUDED
#define NORTHSTARCPP_MEMORY_CHUNK_READER_H_INCLUDED

#include <northstarcpp/compat/memory_reader.h>
#include <northstarcpp/compat/memory_sequential_chunk_reader.h>
#include <northstarcpp/compat/memory_array_chunk_reader.h>
#include <northstarcpp/compat/memory_associative_chunk_reader.h>

namespace northstarcpp {
namespace memory {

class NORTHSTARCPP_CORE_API chunk_reader :
	public sequential_chunk_reader< chunk_reader >,
	public array_chunk_reader< chunk_reader >,
	public associative_chunk_reader< chunk_reader >
{

public:
	enum chunk_type {
		chunk_type_sequential	= u32(0),
		chunk_type_array,
		chunk_type_associative,

		chunk_type_count,
	}; // enum chunk_type

public:
							chunk_reader		( pcbyte data, u32 size );
							chunk_reader		( pcbyte data, u32 size, chunk_type type );
			bool			chunk_exists		( u32 chunk_id );
			chunk_reader	open_chunk_reader	( u32 chunk_id );
			reader			open_reader			( u32 chunk_id );
	inline	pcbyte			data				( ) const { return m_reader.data( ); }

private:
			void			construct			( );
			u32				chunk_size			( u32 const chunk_id, chunk_type& type );
			u32				chunk_position		( u32 chunk_id );
	inline	reader&			reader				( ) { return m_reader; };

private:
	friend class sequential_chunk_reader< chunk_reader >;
	friend class array_chunk_reader< chunk_reader >;
	friend class associative_chunk_reader< chunk_reader >;

private:
	memory::reader			m_reader;
	pcbyte					m_chunks;
	u32						m_last_position;
	u32						m_chunk_count;
	chunk_type				m_type;
}; // class reader

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_CHUNK_READER_H_INCLUDED