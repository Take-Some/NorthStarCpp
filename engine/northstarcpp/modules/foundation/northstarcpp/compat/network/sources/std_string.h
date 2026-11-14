////////////////////////////////////////////////////////////////////////////
//	Created		: 10.12.2009
//	Author		: Alexander Maniluk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef STD_STRING_H_INCLUDED
#define STD_STRING_H_INCLUDED

#include <northstarcpp/compat/fixed_string.h>
#include <string>

namespace northstarcpp {
namespace network {

class std_string : public fixed_string512
{
	typedef fixed_string512	base_class;
public:
				std_string		();
				std_string		(std_string const& src);
				std_string		(buffer_string const& src);
				std_string		(value_type const* src);
				std_string		(value_type	src);
				std_string		(value_type const* src, size_type const& count);
				~std_string		();
	
	value_type const*			data			() const;
	void						erase			(size_t	pos, size_type count = npos);
	std_string	&				operator+		(std_string const & right);
	operator					std::string		() const;
	u32							size			() const;
}; // class std_string

} // namespace network
} // namespace northstarcpp

#include "std_string_inline.h"

#endif // #ifndef STD_STRING_H_INCLUDED