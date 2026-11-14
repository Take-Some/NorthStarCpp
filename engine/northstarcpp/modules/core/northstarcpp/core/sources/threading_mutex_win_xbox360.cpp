////////////////////////////////////////////////////////////////////////////
//	Created 	: 25.09.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/threading_mutex.h>

#define	_WIN32_WINNT 0x0500				// for TryEnterCriticalSection
#include <northstarcpp/os_include.h>

using northstarcpp::threading::mutex;

mutex::mutex		()
{
	COMPILE_ASSERT				( sizeof( CRITICAL_SECTION ) == sizeof( m_mutex ), please_change_buffer_size );
//	InitializeCriticalSection	( &( CRITICAL_SECTION& )m_mutex );
	InitializeCriticalSectionAndSpinCount	( &( CRITICAL_SECTION& )m_mutex, 10000 );
}

mutex::~mutex		()
{
	DeleteCriticalSection		( &( CRITICAL_SECTION& )m_mutex );
}

void mutex::lock	()
{
//	debug::printf				(temp, "lock   0x%x[%d]\r\n", *(u32*)&m_mutex, GetCurrentThreadId());
	EnterCriticalSection		( &( CRITICAL_SECTION& )m_mutex );
}

void mutex::unlock	()
{
//	debug::printf				(temp, "unlock 0x%x[%d]\r\n", *(u32*)&m_mutex, GetCurrentThreadId());
	LeaveCriticalSection		( &( CRITICAL_SECTION& )m_mutex );
}

bool mutex::try_lock	()
{
	return						( !!TryEnterCriticalSection( &( CRITICAL_SECTION& )m_mutex ) );
}