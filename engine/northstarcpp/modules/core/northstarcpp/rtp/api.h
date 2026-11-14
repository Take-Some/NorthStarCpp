////////////////////////////////////////////////////////////////////////////
//	Created		: 23.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RTP_API_H_INCLUDED
#define NORTHSTARCPP_RTP_API_H_INCLUDED

#ifndef NORTHSTARCPP_RTP_API
#	ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		define NORTHSTARCPP_RTP_API
#	else // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#		ifdef NORTHSTARCPP_RTP_BUILDING
#			define NORTHSTARCPP_RTP_API						NORTHSTARCPP_DLL_EXPORT
#		else // #ifdef NORTHSTARCPP_RTP_BUILDING
#			ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RTP_API					NORTHSTARCPP_DLL_IMPORT
#			else // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#				define NORTHSTARCPP_RTP_API					NORTHSTARCPP_DLL_EXPORT
#			endif // #ifndef NORTHSTARCPP_ENGINE_BUILDING
#		endif // #ifdef NORTHSTARCPP_RTP_BUILDING
#	endif // #ifdef NORTHSTARCPP_STATIC_LIBRARIES
#endif // #ifndef NORTHSTARCPP_RTP_API

namespace northstarcpp {
namespace rtp {

struct engine;
struct world;

//typedef memory::doug_lea_allocator_type		allocator_type;
typedef memory::pthreads_allocator_type			allocator_type;

NORTHSTARCPP_RTP_API	world*	create_world	( engine& engine );
NORTHSTARCPP_RTP_API	void	destroy_world	( world*& world );
NORTHSTARCPP_RTP_API	void	memory_allocator( memory::doug_lea_allocator_type& allocator );

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RTP_API_H_INCLUDED