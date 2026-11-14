////////////////////////////////////////////////////////////////////////////
//	Created		: 02.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "mixing_animation_clip.h"
#include "base_interpolator.h"

using northstarcpp::animation::mixing::animation_clip;

bool animation_clip::operator ==			( animation_clip const& other ) const
{
	if ( type() != other.type() )
		return		false;

	if ( compare(interpolator(), other.interpolator()) != interpolator_comparer::equal )
		return		false;

	return			!strings::compare( identifier(), other.identifier() );
}

bool northstarcpp::animation::mixing::operator <	( animation_clip const& left, animation_clip const& right )
{
	if ( left.type() < right.type() )
		return		true;

	if ( right.type() < left.type() )
		return		false;

	switch ( compare(left.interpolator(), right.interpolator()) ) {
		case interpolator_comparer::less : return true;
		case interpolator_comparer::more : return false;
		case interpolator_comparer::equal : 
			return	strings::compare( left.identifier(), right.identifier() ) < 0;
		default :	NODEFAULT(return false);
	}
}