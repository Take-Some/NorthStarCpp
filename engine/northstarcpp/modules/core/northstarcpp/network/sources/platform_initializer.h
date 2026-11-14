////////////////////////////////////////////////////////////////////////////
//	Created		: 29.01.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PLATFORM_INITIALIZER_H_INCLUDED
#define PLATFORM_INITIALIZER_H_INCLUDED

namespace northstarcpp {
namespace network {
namespace lowlevel {


class platform_initializer
{
public:
	platform_initializer	();
	~platform_initializer	();
private:
	char NORTHSTARCPP_DEFAULT_ALIGN	m_object_store[8];
}; // class platform_initializer

} // namespace lowlevel
} // namespace network
} // namespace northstarcpp

#endif // #ifndef PLATFORM_INITIALIZER_H_INCLUDED