////////////////////////////////////////////////////////////////////////////
//	Created		: 29.01.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "platform_initializer.h"
#include "boost_api.h"

namespace northstarcpp {
namespace network {
namespace lowlevel {

#pragma comment(lib, "ws2_32.lib")
typedef boost::asio::detail::winsock_init<> initializer_t;

platform_initializer::platform_initializer()
{
	COMPILE_ASSERT(sizeof(m_object_store) >= sizeof(initializer_t), NotEnoughSpaceToStoreObject);
	new (m_object_store) initializer_t();
}

platform_initializer::~platform_initializer()
{
	initializer_t* tmp_initer	= northstarcpp::pointer_cast<initializer_t*>(m_object_store);
	tmp_initer->~winsock_init	();
	initializer_t::finalize		();
}


} // namespace lowlevel
} // namespace network
} // namespace northstarcpp

