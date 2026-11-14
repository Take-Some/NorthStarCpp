////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MULTIPLICATION_LEXEME_INLINE_H_INCLUDED
#define MULTIPLICATION_LEXEME_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

template < typename T1, typename T2 >
inline multiplication_lexeme::multiplication_lexeme						( T1& left, T2& right ) :
	binary_tree_multiplication_node	(
		cloned_in_buffer( left ),
		cloned_in_buffer( right )
	),
	binary_operation_lexeme ( left.buffer() )
{
	ASSERT					( left.buffer() == right.buffer() );
}

inline multiplication_lexeme::multiplication_lexeme						( multiplication_lexeme const& other, bool ) :
	binary_tree_multiplication_node	( other ),
	binary_operation_lexeme ( other, true )
{
}

inline multiplication_lexeme* multiplication_lexeme::cloned_in_buffer	( )
{
	return					base_lexeme::cloned_in_buffer<multiplication_lexeme>( );
}

template < typename T1, typename T2 >
inline	multiplication_lexeme& operator *								( T1& left, T2& right )
{
	return					*multiplication_lexeme( left, right ).cloned_in_buffer( );
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef MULTIPLICATION_LEXEME_INLINE_H_INCLUDED