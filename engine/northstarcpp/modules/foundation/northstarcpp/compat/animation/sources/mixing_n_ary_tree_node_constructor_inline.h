////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef N_ARY_TREE_NODE_CONSTRUCTOR_INLINE_H_INCLUDED
#define N_ARY_TREE_NODE_CONSTRUCTOR_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline n_ary_tree_base_node* n_ary_tree_node_constructor::result	( ) const
{
	ASSERT	( m_result );
	return	m_result;
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef N_ARY_TREE_NODE_CONSTRUCTOR_INLINE_H_INCLUDED