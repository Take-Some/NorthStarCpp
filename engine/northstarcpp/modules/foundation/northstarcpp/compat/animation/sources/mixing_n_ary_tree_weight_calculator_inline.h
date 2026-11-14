////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXING_N_ARY_TREE_WEIGHT_CALCULATOR_INLINE_H_INCLUDED
#define MIXING_N_ARY_TREE_WEIGHT_CALCULATOR_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline n_ary_tree_weight_calculator::n_ary_tree_weight_calculator	( u32 const current_time_in_ms ) :
	m_result			( 0 ),
	m_current_time_in_ms( current_time_in_ms ),
	m_weight			( 0.f ),
	m_null_weight_found	( false )
{
}

inline float n_ary_tree_weight_calculator::weight					( ) const
{
	return				m_weight;
}

inline bool n_ary_tree_weight_calculator::null_weight_found			( ) const
{
	return				m_null_weight_found;
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MIXING_N_ARY_TREE_WEIGHT_CALCULATOR_INLINE_H_INCLUDED