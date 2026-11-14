////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXING_N_ARY_TREE_INTRUSIVE_BASE_INLINE_H_INCLUDED
#define MIXING_N_ARY_TREE_INTRUSIVE_BASE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline n_ary_tree_intrusive_base::n_ary_tree_intrusive_base	( ) :
	m_reference_count	( 0 )
{
}

template <typename T>
inline void n_ary_tree_intrusive_base::destroy				( T& )
{
}

inline u32 n_ary_tree_intrusive_base::reference_count		( ) const
{
	return				m_reference_count;
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MIXING_N_ARY_TREE_INTRUSIVE_BASE_INLINE_H_INCLUDED