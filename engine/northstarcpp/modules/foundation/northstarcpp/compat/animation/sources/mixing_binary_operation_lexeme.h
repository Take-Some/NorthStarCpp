////////////////////////////////////////////////////////////////////////////
//	Created		: 26.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BINARY_OPERATION_LEXEME_H_INCLUDED
#define BINARY_OPERATION_LEXEME_H_INCLUDED

#include "mixing_base_lexeme.h"
#include "mixing_expression.h"

namespace northstarcpp {
namespace animation {
namespace mixing {

class binary_operation_lexeme : public base_lexeme {
public:
	inline	binary_operation_lexeme	( mutable_buffer& buffer );
	inline	binary_operation_lexeme	( base_lexeme const& other, bool );

private:
	typedef base_lexeme	super;

protected:
	template < typename T >
	static inline	T*						cloned_in_buffer( T& value );
	static inline	binary_tree_base_node*	cloned_in_buffer( expression& value );
}; // class binary_operation_lexeme

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#include "mixing_binary_operation_lexeme_inline.h"

#endif // #ifndef BINARY_OPERATION_LEXEME_H_INCLUDED