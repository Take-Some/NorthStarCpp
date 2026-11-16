////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef FIXED_STRING_H_INCLUDED
#define FIXED_STRING_H_INCLUDED

#include <northstarcpp/buffer_string.h>

namespace northstarcpp {

template <int Size>
class fixed_string : public buffer_string
{
public:
	enum					{ fixed_size = Size };
	typedef	fixed_string	self_type;

public:
	//-----------------------------------------------------------------------------------
	// initializations
	//-----------------------------------------------------------------------------------
							fixed_string	();
							fixed_string	(fixed_string const&	src);
							fixed_string	(buffer_string const&	src);
							fixed_string	(value_type const*		src);
							fixed_string	(value_type				src);
							fixed_string	(value_type const*		src, size_type const& count);
							template <class src_iterator>
							fixed_string	(src_iterator const& begin, src_iterator const& end);

 	fixed_string const&		operator =		(fixed_string const& s);
							template <class src_type>
 	fixed_string const&		operator =		(src_type const& s);

	char*					get_buffer		() { return m_buffer; } // when writing NULL, call set_size
	u32						get_buffer_size	() { return Size; }

	//-----------------------------------------------------------------------------------
	// operations
	//-----------------------------------------------------------------------------------
	void					substr			(size_type pos, size_type count, buffer_string * out_dest) const;
	fixed_string			substr			(size_type pos, size_type count=npos) const;

private:
	char					m_buffer[Size];
};

typedef fixed_string<512>	fixed_string512;
typedef fixed_string<1024>	fixed_string1024;
typedef fixed_string<2048>	fixed_string2048;
typedef fixed_string<4096>	fixed_string4096;
typedef fixed_string<8192>	fixed_string8192;

template class NORTHSTARCPP_CORE_API  fixed_string<512>;
template class NORTHSTARCPP_CORE_API  fixed_string<1024>;
template class NORTHSTARCPP_CORE_API  fixed_string<2048>;
template class NORTHSTARCPP_CORE_API  fixed_string<4096>;
template class NORTHSTARCPP_CORE_API  fixed_string<8192>;

} // namespace northstarcpp

#include <northstarcpp/fixed_string_inline.h>

#endif // FIXED_STRING_H_INCLUDED