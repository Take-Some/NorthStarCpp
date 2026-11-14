////////////////////////////////////////////////////////////////////////////
//	Created		: 04.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "mixing_n_ary_tree_size_calculator.h"
#include "mixing_n_ary_tree_animation_node.h"
#include "mixing_n_ary_tree_transition_node.h"
#include "mixing_n_ary_tree_weight_node.h"
#include "mixing_n_ary_tree_addition_node.h"
#include "mixing_n_ary_tree_subtraction_node.h"
#include "mixing_n_ary_tree_multiplication_node.h"
#include "mixing_binary_tree_addition_node.h"
#include "mixing_binary_tree_subtraction_node.h"
#include "mixing_binary_tree_multiplication_node.h"

using northstarcpp::animation::mixing::n_ary_tree_size_calculator;

using northstarcpp::animation::mixing::binary_tree_animation_node;
using northstarcpp::animation::mixing::binary_tree_weight_node;
using northstarcpp::animation::mixing::binary_tree_addition_node;
using northstarcpp::animation::mixing::binary_tree_multiplication_node;
using northstarcpp::animation::mixing::binary_tree_subtraction_node;
using northstarcpp::animation::mixing::binary_tree_binary_operation_node;

using northstarcpp::animation::mixing::n_ary_tree_animation_node;
using northstarcpp::animation::mixing::n_ary_tree_transition_node;
using northstarcpp::animation::mixing::n_ary_tree_weight_node;
using northstarcpp::animation::mixing::n_ary_tree_addition_node;
using northstarcpp::animation::mixing::n_ary_tree_multiplication_node;
using northstarcpp::animation::mixing::n_ary_tree_subtraction_node;

n_ary_tree_size_calculator::n_ary_tree_size_calculator	( ) :
	m_size						( 0 )
{
}

void n_ary_tree_size_calculator::visit					( binary_tree_animation_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_animation_node);
}

void n_ary_tree_size_calculator::visit					( binary_tree_weight_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_weight_node);
	m_size						+= sizeof(n_ary_tree_base_node*);
}

void n_ary_tree_size_calculator::visit					( binary_tree_addition_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_addition_node);
	m_size						+= sizeof(n_ary_tree_base_node*);
	propagate					( static_cast<binary_tree_binary_operation_node&>(node) );
}

void n_ary_tree_size_calculator::visit					( binary_tree_subtraction_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_subtraction_node);
	m_size						+= sizeof(n_ary_tree_base_node*);
	propagate					( static_cast<binary_tree_binary_operation_node&>(node) );
}

void n_ary_tree_size_calculator::visit					( binary_tree_multiplication_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_multiplication_node);
	m_size						+= sizeof(n_ary_tree_base_node*);
	propagate					( static_cast<binary_tree_binary_operation_node&>(node) );
}

void n_ary_tree_size_calculator::propagate				( binary_tree_binary_operation_node& node )
{
	node.left().accept			( *this );
	node.right().accept			( *this );
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_animation_node& node )
{
	propagate					( node );
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_transition_node& node )
{
	m_size						+= sizeof(n_ary_tree_transition_node);
	node.from().accept			( *this );
	node.to().accept			( *this );
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_weight_node& node )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( node );
	m_size						+= sizeof(n_ary_tree_weight_node);
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_addition_node& node )
{
	propagate					( node );
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_subtraction_node& node )
{
	propagate					( node );
}

void n_ary_tree_size_calculator::visit					( n_ary_tree_multiplication_node& node )
{
	propagate					( node );
}

template < typename T >
inline void n_ary_tree_size_calculator::propagate		( T& node )
{
	m_size						+= sizeof(T);

	u32 const operands_count	= node.operands_count( );
	m_size						+= operands_count*sizeof(n_ary_tree_base_node*);

	n_ary_tree_base_node* const* i		= node.operands( sizeof(T) );
	n_ary_tree_base_node* const* const e	= i + operands_count;
	for ( ; i != e; ++i )
		(*i)->accept			( *this );
}