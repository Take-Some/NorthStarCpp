////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "instant_interpolator.h"
#include "interpolator_comparer.h"

using northstarcpp::animation::instant_interpolator;
using northstarcpp::animation::interpolator_comparer;
using northstarcpp::animation::interpolator_visitor;
using northstarcpp::animation::linear_interpolator;
using northstarcpp::animation::fermi_dirac_interpolator;

float instant_interpolator::interpolated_value		( float current_transition_time ) const
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( current_transition_time );
	return				1.f;
}

float instant_interpolator::transition_time			( ) const
{
	return				0.f;
}

instant_interpolator* instant_interpolator::clone	( northstarcpp::mutable_buffer& buffer ) const
{
	return				clone_impl< instant_interpolator >( buffer );
}

void instant_interpolator::accept					( interpolator_visitor& visitor ) const
{
	visitor.visit		( *this );
}

void instant_interpolator::accept					( interpolator_comparer& dispatcher, base_interpolator const& interpolator ) const
{
	interpolator.visit	( dispatcher, *this );
}

void instant_interpolator::visit					( interpolator_comparer& dispatcher, instant_interpolator const& interpolator ) const
{
	dispatcher.dispatch	( interpolator, *this );
}

void instant_interpolator::visit					( interpolator_comparer& dispatcher, linear_interpolator const& interpolator ) const
{
	dispatcher.dispatch	( interpolator, *this );
}

void instant_interpolator::visit					( interpolator_comparer& dispatcher, fermi_dirac_interpolator const& interpolator ) const
{
	dispatcher.dispatch	( interpolator, *this );
}