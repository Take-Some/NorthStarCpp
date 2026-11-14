////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LOOSE_PTR_DATA_H_INCLUDED
#define NORTHSTARCPP_LOOSE_PTR_DATA_H_INCLUDED

namespace northstarcpp {

class loose_ptr_base;

class loose_ptr_data {
public:
	inline			loose_ptr_data	( loose_ptr_base* const pointer );
	template < typename T >
	inline	void	destroy			( T* object ) const;

public:
	loose_ptr_base*					m_pointer;
	threading::atomic32_type		m_reference_count;
};

} // namespace northstarcpp

#include <northstarcpp/loose_ptr_data_inline.h>

#endif // #ifndef NORTHSTARCPP_LOOSE_PTR_DATA_H_INCLUDED