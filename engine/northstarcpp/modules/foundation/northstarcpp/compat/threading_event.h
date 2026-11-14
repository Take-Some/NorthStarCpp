////////////////////////////////////////////////////////////////////////////
//	Created 	: 09.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_THREADING_EVENT_H_INCLUDED
#define NORTHSTARCPP_THREADING_EVENT_H_INCLUDED

namespace northstarcpp {
namespace threading {

class NORTHSTARCPP_CORE_API event : detail::noncopyable 
{
public:
	enum wait_result_enum			{ wait_result_failed, wait_result_signaled, wait_result_still_nonsignaled };
	static u32 const				wait_time_infinite;

public:
						event		( bool initial_state = false );
					   ~event		( );
public:

	void				set			( bool value);
	wait_result_enum	wait		( u32 max_wait_time_ms );

public:
	pvoid				get_handle	( ) const;

private:
#if NORTHSTARCPP_PLATFORM_WINDOWS | NORTHSTARCPP_PLATFORM_XBOX_360
	char NORTHSTARCPP_DEFAULT_ALIGN	m_event[sizeof(pvoid)]; // sizeof(CRITICAL_SECTION);
#elif NORTHSTARCPP_PLATFORM_PS3 // #if NORTHSTARCPP_PLATFORM_WINDOWS | NORTHSTARCPP_PLATFORM_XBOX_360
	char NORTHSTARCPP_DEFAULT_ALIGN	m_event[4];				// sizeof(sys_event_flag_t);
#endif // #elif NORTHSTARCPP_PLATFORM_XBOX_360
}; // class event

} // namespace threading
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_THREADING_EVENT_H_INCLUDED