////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXING_N_ARY_TREE_INTRUSIVE_BASE_H_INCLUDED
#define MIXING_N_ARY_TREE_INTRUSIVE_BASE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

class n_ary_tree_intrusive_base {
public:
	inline		n_ary_tree_intrusive_base	( );

	template <typename T>
	static	inline	void	destroy			( T& );

	inline	u32				reference_count	( ) const;

private:
	friend class threading::single_threading_policy;
	u32		m_reference_count;
}; // class n_ary_tree_intrusive_base

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#include "mixing_n_ary_tree_intrusive_base_inline.h"

#endif // #ifndef MIXING_N_ARY_TREE_INTRUSIVE_BASE_H_INCLUDED