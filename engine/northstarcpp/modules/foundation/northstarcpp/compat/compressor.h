////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_COMPRESSOR_H_INCLUDED
#define NORTHSTARCPP_COMPRESSOR_H_INCLUDED

#include <northstarcpp/compat/memory_buffer.h>
#include <northstarcpp/compat/debug_signalling_bool.h>

namespace northstarcpp {

class NORTHSTARCPP_CORE_API compressor
{
public:
	virtual northstarcpp::signalling_bool	compress	(const_buffer src, mutable_buffer dest, u32& out_size) = 0;
	virtual northstarcpp::signalling_bool	decompress	(const_buffer src, mutable_buffer dest, u32& out_size) = 0;

	virtual ~compressor () {}

}; // class compressor

} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_PPMD_ALLOCATOR_H_INCLUDED