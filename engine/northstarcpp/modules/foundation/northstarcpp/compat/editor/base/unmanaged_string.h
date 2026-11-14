////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef UNMANAGED_STRING_H_INCLUDED
#define UNMANAGED_STRING_H_INCLUDED

class unmanaged_string : private boost::noncopyable {
public:
	inline			unmanaged_string	( System::String^ string );
	inline			~unmanaged_string	( );
	inline	pstr	c_str				( ) const;

private:
	pstr	m_string;
}; // class unmanaged_string

#include "unmanaged_string_inline.h"

#endif // #ifndef UNMANAGED_STRING_H_INCLUDED