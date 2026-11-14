////////////////////////////////////////////////////////////////////////////
//	Created		: 04.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef N_ARY_TREE_VISITOR_H_INCLUDED
#define N_ARY_TREE_VISITOR_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

class n_ary_tree_animation_node;
class n_ary_tree_weight_node;
class n_ary_tree_addition_node;
class n_ary_tree_subtraction_node;
class n_ary_tree_multiplication_node;
class n_ary_tree_transition_node;

struct NORTHSTARCPP_NOVTABLE n_ary_tree_visitor {
	virtual	void	visit		( n_ary_tree_animation_node& node ) = 0;
	virtual	void	visit		( n_ary_tree_transition_node& node ) = 0;
	virtual	void	visit		( n_ary_tree_weight_node& node ) = 0;
	virtual	void	visit		( n_ary_tree_addition_node& node ) = 0;
	virtual	void	visit		( n_ary_tree_subtraction_node& node ) = 0;
	virtual	void	visit		( n_ary_tree_multiplication_node& node ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( n_ary_tree_visitor )
}; // class n_ary_tree_visitor

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef N_ARY_TREE_VISITOR_H_INCLUDED