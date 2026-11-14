////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include <northstarcpp/logging_settings.h>
#include "logging_path_parts.h"

using northstarcpp::logging::settings;

settings::settings	(pcstr initiator_prefix, u32 flags) : flags((flags_enum)flags)
{
	if ( initiator_prefix && *initiator_prefix )
	{
		COMPILE_ASSERT		(sizeof(m_initiator_prefix) > 32, check_initiator_prefix_to_be_char_array_type);

		northstarcpp::buffer_string	prefix_buffer(m_initiator_prefix, sizeof(m_initiator_prefix));

		prefix_buffer	+=	initiator_prefix;
		prefix_buffer	+=	logging::initiator_separator;
	}
	else
		m_initiator_prefix[0]	=	0;
}
