////////////////////////////////////////////////////////////////////////////
//	Created		: 04.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#ifndef MASTER_GOLD
int _matherr	( struct _exception *exception )
{
	return		( northstarcpp::debug::on_math_error( exception ) );
}
#endif // #ifndef MASTER_GOLD