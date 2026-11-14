////////////////////////////////////////////////////////////////////////////
//	Created		: 26.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BINARY_TREE_BINARY_OPERATION_NODE_INLINE_H_INCLUDED
#define BINARY_TREE_BINARY_OPERATION_NODE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline binary_tree_binary_operation_node::binary_tree_binary_operation_node	(
		binary_tree_base_node* const left,
		binary_tree_base_node* const right
	) :
	m_left	( left ),
	m_right	( right )
{
}

inline binary_tree_base_node& binary_tree_binary_operation_node::left		( ) const
{
	ASSERT	( m_left );
	return	*m_left;
}

inline binary_tree_base_node& binary_tree_binary_operation_node::right		( ) const
{
	ASSERT	( m_right );
	return	*m_right;
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef BINARY_TREE_BINARY_OPERATION_NODE_INLINE_H_INCLUDED