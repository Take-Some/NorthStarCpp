////////////////////////////////////////////////////////////////////////////
//	Created		: 26.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BINARY_OPERATION_LEXEME_INLINE_H_INCLUDED
#define BINARY_OPERATION_LEXEME_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

inline binary_operation_lexeme::binary_operation_lexeme					( mutable_buffer& buffer ) :
	super	( buffer )
{
}

inline binary_operation_lexeme::binary_operation_lexeme					( base_lexeme const& other, bool ) :
	super	( other, true )
{
}

template < typename T >
inline T* binary_operation_lexeme::cloned_in_buffer						( T& value )
{
	return	value.cloned_in_buffer();
}

inline binary_tree_base_node* binary_operation_lexeme::cloned_in_buffer	( expression& value )
{
	return	&value.node();
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef BINARY_OPERATION_LEXEME_INLINE_H_INCLUDED