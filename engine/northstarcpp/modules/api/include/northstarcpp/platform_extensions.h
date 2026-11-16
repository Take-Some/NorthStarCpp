////////////////////////////////////////////////////////////////////////////
//	Created 	: 13.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_PLATFORM_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_PLATFORM_EXTENSIONS_H_INCLUDED

namespace northstarcpp {
namespace platform {

inline bool	little_endian	( )
{
	u16	const word	= 0x0001;
	return			( !!*( u8 const* )&word );
}

inline bool	big_endian		( )
{
	return			( !little_endian( ) );
}

} // namespace platform
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_PLATFORM_EXTENSIONS_H_INCLUDED