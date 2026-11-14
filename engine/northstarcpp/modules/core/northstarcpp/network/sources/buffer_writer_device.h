////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BUFFER_WRITER_DEVICE_H_INCLUDED
#define BUFFER_WRITER_DEVICE_H_INCLUDED

#include <northstarcpp/debug_signalling_bool.h>

namespace northstarcpp
{

class buffer_writer_device
{
public:
	typedef	u32							size_type;
	typedef	pbyte						pointer_type;
	typedef pcbyte						const_pointer_type;
	typedef northstarcpp::mutable_buffer		buffer_type;

	inline			buffer_writer_device	(pointer_type buff,
											 size_type const & buff_size);
	inline			buffer_writer_device	(buffer_type & buf);
	inline			~buffer_writer_device	();
	
	//core device interface
	inline	void			write			(const_pointer_type const ptr,
											 size_type const size);
	//seekable interface
	inline	void			seek			(size_type const new_pos);
	inline	size_type		position		() const { return m_pos; };
	inline	size_type		size			() const { return m_buffer.size(); }
private:
	inline	size_type		capacity		() const;
	inline	pointer_type	current			();

	buffer_type			m_buffer;
	size_type			m_pos;
}; // class buffer_writer_device

} // namespace northstarcpp

#include "buffer_writer_device_inline.h"

#endif // #ifndef BUFFER_WRITER_DEVICE_H_INCLUDED