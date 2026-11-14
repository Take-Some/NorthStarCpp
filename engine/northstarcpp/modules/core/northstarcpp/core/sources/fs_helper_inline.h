////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef FS_HELPER_INLINE_H_INCLUDED
#define FS_HELPER_INLINE_H_INCLUDED

namespace northstarcpp {
namespace fs {

template <class Ordinal>
void	  reverse_bytes (Ordinal & res)
{
	char* const	it_begin			=	reinterpret_cast<char*>(&res);
	char* const	it_end				=	it_begin + sizeof(Ordinal);
	std::reverse						(it_begin, it_end);
}

} // namespace fs
} // namespace northstarcpp 

#endif // FS_HELPER_INLINE_H_INCLUDED