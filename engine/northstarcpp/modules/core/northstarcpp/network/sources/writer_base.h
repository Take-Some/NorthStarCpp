////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef WRITER_BASE_H_INCLUDED
#define WRITER_BASE_H_INCLUDED

namespace northstarcpp
{

template <typename Writer, typename Device>
class seekable_writer
{
public:
	typedef Device								device_type;
	typedef	typename Device::size_type			size_type;
	typedef	typename Device::pointer_type		pointer_type;
	typedef typename Device::const_pointer_type	const_pointer_type;

	void				seek		(size_type const new_pos);	
	size_type const		position	() const;
	size_type const		size		() const { return position(); }
}; // class seekable_writer

} // namespace northstarcpp

#include "writer_base_inline.h"

#endif // #ifndef WRITER_BASE_H_INCLUDED