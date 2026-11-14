////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "mixing_binary_tree_animation_node.h"
#include "mixing_animation_clip.h"
#include "base_interpolator.h"
#include "mixing_binary_tree_visitor.h"

using northstarcpp::animation::mixing::binary_tree_animation_node;
using northstarcpp::animation::mixing::binary_tree_visitor;
using northstarcpp::animation::mixing::binary_tree_base_node;
using northstarcpp::animation::base_interpolator;

void binary_tree_animation_node::accept								( binary_tree_visitor& visitor )
{
	visitor.visit	( *this );
}

base_interpolator const* binary_tree_animation_node::interpolator	( ) const
{
	return			&animation()->interpolator();
}