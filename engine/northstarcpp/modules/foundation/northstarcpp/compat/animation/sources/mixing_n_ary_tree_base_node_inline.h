////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MIXING_N_ARY_TREE_BASE_NODE_INLINE_H_INCLUDED
#define MIXING_N_ARY_TREE_BASE_NODE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline n_ary_tree_base_node::n_ary_tree_base_node	( )
{
#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
	++g_n_ary_tree_node_constructors;
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
}

#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
inline n_ary_tree_base_node::n_ary_tree_base_node	( n_ary_tree_base_node const& )
{
	++g_n_ary_tree_node_constructors;
}
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS

inline n_ary_tree_base_node::~n_ary_tree_base_node	( )
{
#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
	ASSERT	( g_n_ary_tree_node_destructors < g_n_ary_tree_node_constructors );
	++g_n_ary_tree_node_destructors;
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MIXING_N_ARY_TREE_BASE_NODE_INLINE_H_INCLUDED