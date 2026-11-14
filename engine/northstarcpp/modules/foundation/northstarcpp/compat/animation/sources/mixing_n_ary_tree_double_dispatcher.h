////////////////////////////////////////////////////////////////////////////
//	Created		: 05.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef N_ARY_TREE_DOUBLE_DISPATCHER_H_INCLUDED
#define N_ARY_TREE_DOUBLE_DISPATCHER_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

struct n_ary_tree_base_node;
class n_ary_tree_animation_node;
class n_ary_tree_weight_node;
class n_ary_tree_addition_node;
class n_ary_tree_subtraction_node;
class n_ary_tree_multiplication_node;
class n_ary_tree_transition_node;

struct NORTHSTARCPP_NOVTABLE n_ary_tree_double_dispatcher {
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_animation_node& left,		n_ary_tree_multiplication_node& right ) = 0;

	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_transition_node& left,		n_ary_tree_multiplication_node& right ) = 0;

	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_weight_node& left,			n_ary_tree_multiplication_node& right ) = 0;

	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_addition_node& left,		n_ary_tree_multiplication_node& right ) = 0;

	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_subtraction_node& left,	n_ary_tree_multiplication_node& right ) = 0;

	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_animation_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_transition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_weight_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_addition_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_subtraction_node& right ) = 0;
	virtual	void	dispatch	( n_ary_tree_multiplication_node& left,	n_ary_tree_multiplication_node& right ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( n_ary_tree_double_dispatcher )
}; // class n_ary_tree_double_dispatcher

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef N_ARY_TREE_DOUBLE_DISPATCHER_H_INCLUDED