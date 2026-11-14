////////////////////////////////////////////////////////////////////////////
//	Created		: 25.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SUBTRACTION_LEXEME_INLINE_H_INCLUDED
#define SUBTRACTION_LEXEME_INLINE_H_INCLUDED

namespace northstarcpp {
namespace animation {
namespace mixing {

template < typename T1, typename T2 >
inline subtraction_lexeme::subtraction_lexeme					( T1& left, T2& right ) :
	binary_tree_subtraction_node	(
		cloned_in_buffer( left ),
		cloned_in_buffer( right )
	),
	binary_operation_lexeme ( left.buffer() )
{
	ASSERT					( left.buffer() == right.buffer() );
}

inline subtraction_lexeme::subtraction_lexeme					( subtraction_lexeme const& other, bool ) :
	binary_tree_subtraction_node	( other ),
	binary_operation_lexeme ( other, true )
{
}

inline subtraction_lexeme* subtraction_lexeme::cloned_in_buffer	( )
{
	return					base_lexeme::cloned_in_buffer< subtraction_lexeme >( );
}

template < typename T1, typename T2 >
inline subtraction_lexeme& operator -							( T1& left, T2& right )
{
	return					*subtraction_lexeme( left, right ).cloned_in_buffer( );
}

} // namespace mixing
} // namespace animation
} // namespace northstarcpp

#endif // #ifndef SUBTRACTION_LEXEME_INLINE_H_INCLUDED