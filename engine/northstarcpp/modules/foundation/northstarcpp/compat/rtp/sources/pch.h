////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_RTP_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"rtp"
#include <northstarcpp/compat/extensions.h>

#include "rtp_memory.h"

namespace northstarcpp {
	namespace math {
		class random32;
	} //  namespace math  

	//namespace rtp {
	//	extern northstarcpp::math::random32		*g_random;
	//} // namespace rtp 

	namespace render {
		namespace debug {
			struct renderer;
		} // namespace debug
	} // namespace render
} //  namespace northstarcpp 



#endif // #ifndef PCH_H_INCLUDED