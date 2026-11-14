////////////////////////////////////////////////////////////////////////////
//	Created		: 09.06.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"

#ifndef MASTER_GOLD
int _matherr	( struct _exception *exception )
{
	return		( northstarcpp::debug::on_math_error( exception ) );
}
#endif // #ifndef MASTER_GOLD