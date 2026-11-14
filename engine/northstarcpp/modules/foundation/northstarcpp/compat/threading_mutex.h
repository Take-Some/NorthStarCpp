////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_THREADING_MUTEX_H_INCLUDED
#define NORTHSTARCPP_THREADING_MUTEX_H_INCLUDED

namespace northstarcpp {
namespace threading {

class NORTHSTARCPP_CORE_API mutex : private detail::noncopyable {
public:
					mutex		( );
					~mutex		( );

public:
			void	lock		( );
			void	unlock		( );
			bool	try_lock	( );

private:
#if NORTHSTARCPP_PLATFORM_WINDOWS_32
	char NORTHSTARCPP_DEFAULT_ALIGN	m_mutex[24]; // sizeof(CRITICAL_SECTION);
#elif NORTHSTARCPP_PLATFORM_WINDOWS_64 // #if NORTHSTARCPP_PLATFORM_WINDOWS_32
	char NORTHSTARCPP_DEFAULT_ALIGN	m_mutex[40]; // sizeof(CRITICAL_SECTION);
#elif NORTHSTARCPP_PLATFORM_XBOX_360 // #elif NORTHSTARCPP_PLATFORM_WINDOWS_64
	char NORTHSTARCPP_DEFAULT_ALIGN	m_mutex[28]; // sizeof(CRITICAL_SECTION);
#elif NORTHSTARCPP_PLATFORM_PS3 // #elif NORTHSTARCPP_PLATFORM_XBOX_360
	char NORTHSTARCPP_DEFAULT_ALIGN	m_mutex[24]; // sizeof(sys_lwmutex_t);
#endif // #elif NORTHSTARCPP_PLATFORM_XBOX_360
};

class mutex_raii : private boost::noncopyable {
public:
	inline			mutex_raii	( mutex& mutex ) : m_mutex( mutex )	{ m_mutex.lock		( ); }
					~mutex_raii	( )									{ m_mutex.unlock	( ); }

private:
	mutex&			m_mutex;
};

} // namespace threading
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_THREADING_MUTEX_H_INCLUDED