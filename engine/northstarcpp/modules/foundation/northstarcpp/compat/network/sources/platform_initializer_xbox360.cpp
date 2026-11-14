////////////////////////////////////////////////////////////////////////////
//	Created		: 29.01.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "platform_initializer.h"

#pragma comment(lib, "Xonline.lib")
#pragma comment(lib, "Xnet.lib")

namespace northstarcpp {
namespace network {
namespace lowlevel {

typedef boost::asio::detail::winsock_init<> initializer_t;

platform_initializer::platform_initializer()
{
}

platform_initializer::~platform_initializer()
{
}


} // namespace lowlevel
} // namespace network
} // namespace northstarcpp