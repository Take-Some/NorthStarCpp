////////////////////////////////////////////////////////////////////////////
//	Created		: 03.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BINARY_TREE_VISITOR_H_INCLUDED
#define BINARY_TREE_VISITOR_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

class binary_tree_animation_node;
class binary_tree_weight_node;
class binary_tree_addition_node;
class binary_tree_subtraction_node;
class binary_tree_multiplication_node;

struct NORTHSTARCPP_NOVTABLE binary_tree_visitor {
	virtual	void	visit			( binary_tree_animation_node& node ) = 0;
	virtual	void	visit			( binary_tree_weight_node& node ) = 0;
	virtual	void	visit			( binary_tree_addition_node& node ) = 0;
	virtual	void	visit			( binary_tree_subtraction_node& node ) = 0;
	virtual	void	visit			( binary_tree_multiplication_node& node ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( binary_tree_visitor )
}; // struct binary_tree_visitor

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef BINARY_TREE_VISITOR_H_INCLUDED