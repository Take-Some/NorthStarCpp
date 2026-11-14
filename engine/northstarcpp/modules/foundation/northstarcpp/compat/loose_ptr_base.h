////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LOOSE_PTR_BASE_H_INCLUDED
#define NORTHSTARCPP_LOOSE_PTR_BASE_H_INCLUDED

#include <northstarcpp/compat/loose_ptr_data.h>

namespace northstarcpp {

class loose_ptr_base {
public:
	inline			loose_ptr_base	( );
	inline			~loose_ptr_base	( );

	loose_ptr_data*	m_pointer;
}; // struct loose_ptr_base

} // namespace northstarcpp

#include <northstarcpp/compat/loose_ptr_base_inline.h>

#endif // #ifndef NORTHSTARCPP_LOOSE_PTR_BASE_H_INCLUDED