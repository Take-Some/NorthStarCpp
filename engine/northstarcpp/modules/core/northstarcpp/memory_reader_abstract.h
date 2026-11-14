////////////////////////////////////////////////////////////////////////////
//	Created 	: 19.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_READER_ABSTRACT_H_INCLUDED
#define NORTHSTARCPP_MEMORY_READER_ABSTRACT_H_INCLUDED

namespace northstarcpp {
namespace memory {

class NORTHSTARCPP_CORE_API reader_abstract {
public:
	virtual	void	r	( pvoid& buffer, u32 size ) = 0;
}; // class reader_abstract

} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_READER_ABSTRACT_H_INCLUDED