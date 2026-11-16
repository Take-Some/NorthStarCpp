////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_TYPE_ENUM_FLAGS_H_INCLUDED
#define NORTHSTARCPP_TYPE_ENUM_FLAGS_H_INCLUDED

namespace northstarcpp {

template <class enum_type>
class NORTHSTARCPP_CORE_API enum_flags
{
public:
	enum_flags (u32 flags = 0) : m_flags(flags) {}

	enum_flags	operator |=	(u32 flags) 				{ m_flags |= flags; return *this; }
	enum_flags	operator &=	(u32 flags) 				{ m_flags &= flags; return *this; }

	enum_flags	operator |  (u32 flags) 		const 	{ return enum_flags(m_flags | flags); }
	enum_flags	operator |  (enum_type flags) 	const 	{ return enum_flags(m_flags | flags); }
	enum_flags	operator &  (u32 flags) 		const 	{ return enum_flags(m_flags & flags); }
	enum_flags	operator &  (enum_type flags) 	const 	{ return enum_flags(m_flags & flags); }
	enum_flags	operator ~	()					const 	{ return enum_flags(~m_flags); }

	operator	bool		()					const	{ return !!m_flags; }
	operator	u32			()					const 	{ return m_flags; }
	operator	enum_type	()					const 	{ return (enum_type)m_flags; }

private:
	u32			m_flags;
};

} // namespace northstarcpp

#endif // /#ifndef NORTHSTARCPP_TYPE_ENUM_FLAGS_H_INCLUDED