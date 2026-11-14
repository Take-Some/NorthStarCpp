////////////////////////////////////////////////////////////////////////////
//	Created		: 20.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BINARY_TREE_BASE_NODE_INLINE_H_INCLUDED
#define BINARY_TREE_BASE_NODE_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline binary_tree_base_node::binary_tree_base_node	( )	:
	m_next_weight				( 0 ),
	m_same_weight				( 0 ),
	m_reference_count			( 0 ),
	m_next_unique_interpolator	( 0 )
{
#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
	++g_binary_tree_node_constructors;
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
}

#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
inline binary_tree_base_node::binary_tree_base_node	( binary_tree_base_node const& other ) :
	m_next_weight				( other.m_next_weight ),
	m_same_weight				( other.m_same_weight ),
	m_reference_count			( other.m_reference_count ),
	m_next_unique_interpolator	( other.m_next_unique_interpolator )
{
	++g_binary_tree_node_constructors;
}
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS

inline binary_tree_base_node::~binary_tree_base_node( )
{
#ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
	ASSERT						( g_binary_tree_node_destructors < g_binary_tree_node_constructors );
	++g_binary_tree_node_destructors;
#endif // #ifdef NORTHSTARCPP_ANIMATION_CHECK_CONSTRUCTORS
}

template < typename T >
inline void binary_tree_base_node::destroy	( T* object )
{
	object->~T					( );
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef BINARY_TREE_BASE_NODE_INLINE_H_INCLUDED