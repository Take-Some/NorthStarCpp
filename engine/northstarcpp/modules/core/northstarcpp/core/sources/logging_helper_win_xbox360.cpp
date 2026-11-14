////////////////////////////////////////////////////////////////////////////
//	Created		: 30.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/os_include.h>

namespace northstarcpp {
namespace logging {

void fill_local_time			( northstarcpp::buffer_string& dest )
{
	SYSTEMTIME					date_time;
	GetLocalTime				( &date_time );
	dest.appendf				(" %02d:%02d:%02d:%03d}", 
								 date_time.wHour, 
								 date_time.wMinute, 
								 date_time.wSecond, 
								 date_time.wMilliseconds);
}

} // namespace logging
} // namespace northstarcpp