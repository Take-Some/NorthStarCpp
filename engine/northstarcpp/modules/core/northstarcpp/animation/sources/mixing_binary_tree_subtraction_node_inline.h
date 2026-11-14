////////////////////////////////////////////////////////////////////////////
//	Created		: 25.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SUBTRACTION_NODE_INLINE_H_INCLUDED
#define SUBTRACTION_NODE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline binary_tree_subtraction_node::binary_tree_subtraction_node		(
		binary_tree_base_node* const left,
		binary_tree_base_node* const right
	) :
	super	( left, right )
{
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef SUBTRACTION_NODE_INLINE_H_INCLUDED