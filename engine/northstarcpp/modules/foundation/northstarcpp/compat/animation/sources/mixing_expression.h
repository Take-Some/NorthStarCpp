////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EXPRESSION_H_INCLUDED
#define EXPRESSION_H_INCLUDED

#include "mixing_base_lexeme.h"
#include "mixing_binary_tree_base_node.h"

namespace northstarcpp {
namespace animation {
namespace mixing {

class base_lexeme;

class expression {
public:
	template < typename T >
	inline						expression		( T& lexeme );
	inline	mutable_buffer&		buffer			( ) const;
	inline	binary_tree_base_node& node			( ) const;
	inline	base_lexeme&		lexeme			( ) const;
	inline	base_lexeme*		cloned_in_buffer( ) const;

private:
	binary_tree_base_node_ptr	m_node;
	base_lexeme*				m_lexeme;
}; // class expression

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#include "mixing_expression_inline.h"

#endif // #ifndef EXPRESSION_H_INCLUDED