////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ADDITION_LEXEME_H_INCLUDED
#define ADDITION_LEXEME_H_INCLUDED

#include "mixing_binary_tree_addition_node.h"
#include "mixing_binary_operation_lexeme.h"
#include "mixing_expression.h"

namespace northstarcpp {
namespace animation {
namespace mixing {

class addition_lexeme :
	public binary_tree_addition_node,
	public binary_operation_lexeme
{
public:
	template < typename T1, typename T2 >
	inline					addition_lexeme		( T1& left, T2& right );
	inline					addition_lexeme		( addition_lexeme const& other, bool );
	inline	addition_lexeme* cloned_in_buffer	( );

protected:
	using binary_operation_lexeme::cloned_in_buffer;
}; // class addition_lexeme

	template < typename T1, typename T2 >
	inline addition_lexeme&	operator +			( T1& left, T2& right );

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#include "mixing_addition_lexeme_inline.h"

#endif // #ifndef ADDITION_LEXEME_H_INCLUDED