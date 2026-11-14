////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_TIMING_TIMER_INLINE_H_INCLUDED
#define NORTHSTARCPP_TIMING_TIMER_INLINE_H_INCLUDED

inline northstarcpp::timing::timer::timer					( )
{
	m_start_time	= 0;
}
inline void northstarcpp::timing::timer::start				( )
{
	m_start_time	= get_QPC( );
}

inline u64 northstarcpp::timing::timer::get_elapsed_ticks	( ) const
{
	return			( get_QPC( ) - m_start_time );
}

inline u32 northstarcpp::timing::timer::get_elapsed_ms		( ) const
{
	return			( u32( get_elapsed_ticks( )*u64( 1000 ) / g_qpc_per_second ) );
}

inline float northstarcpp::timing::timer::get_elapsed_sec	( ) const
{
	return			( float( double(get_elapsed_ticks())/g_qpc_per_second ) );
}

#endif // #ifndef NORTHSTARCPP_TIMING_TIMER_INLINE_H_INCLUDED