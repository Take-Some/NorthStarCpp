////////////////////////////////////////////////////////////////////////////
//	Created		: 29.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "interpolator_size_calculator.h"
#include "instant_interpolator.h"
#include "linear_interpolator.h"
#include "fermi_dirac_interpolator.h"

using northstarcpp::animation::interpolator_size_calculator;
using northstarcpp::animation::instant_interpolator;
using northstarcpp::animation::linear_interpolator;
using northstarcpp::animation::fermi_dirac_interpolator;

bool interpolator_size_calculator::visit	( instant_interpolator& interpolator )
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( interpolator );
	m_size						+= sizeof( instant_interpolator );
	return						true;
}

bool interpolator_size_calculator::visit	( instant_interpolator const& interpolator ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( interpolator );
	m_size						+= sizeof( instant_interpolator );
	return						true;
}

bool interpolator_size_calculator::visit	( linear_interpolator const& interpolator ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( interpolator );
	m_size						+= sizeof( linear_interpolator );
	return						true;
}

bool interpolator_size_calculator::visit	( fermi_dirac_interpolator const& interpolator ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( interpolator );
	m_size						+= sizeof( fermi_dirac_interpolator );
	return						true;
}
