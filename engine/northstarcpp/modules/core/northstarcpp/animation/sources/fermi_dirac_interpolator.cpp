////////////////////////////////////////////////////////////////////////////
//	Created		: 02.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "fermi_dirac_interpolator.h"
#include "interpolator_comparer.h"
#include "../../../../animation/transition_curve.h"

using northstarcpp::animation::fermi_dirac_interpolator;
using northstarcpp::animation::interpolator_comparer;
using northstarcpp::animation::interpolator_visitor;
using northstarcpp::animation::instant_interpolator;
using northstarcpp::animation::linear_interpolator;

fermi_dirac_interpolator::fermi_dirac_interpolator			( float const transition_time, float const epsilon ) :
	m_total_transition_time	( transition_time ),
	m_epsilon				( epsilon )
{
}

float fermi_dirac_interpolator::interpolated_value			( float current_transition_time ) const
{
	R_ASSERT				( current_transition_time < m_total_transition_time );

	return fermi_dirac_weight(current_transition_time, m_total_transition_time, m_epsilon);
}

float fermi_dirac_interpolator::transition_time				( ) const
{
	return					m_total_transition_time;
}

fermi_dirac_interpolator* fermi_dirac_interpolator::clone	( northstarcpp::mutable_buffer& buffer ) const
{
	return					clone_impl< fermi_dirac_interpolator >( buffer );
}

void fermi_dirac_interpolator::accept						( interpolator_visitor& visitor ) const
{
	visitor.visit			( *this );
}

void fermi_dirac_interpolator::accept						( interpolator_comparer& dispatcher, base_interpolator const& interpolator ) const
{
	interpolator.visit		( dispatcher, *this );
}

void fermi_dirac_interpolator::visit						( interpolator_comparer& dispatcher, instant_interpolator const& interpolator ) const
{
	dispatcher.dispatch		( interpolator, *this );
}

void fermi_dirac_interpolator::visit						( interpolator_comparer& dispatcher, linear_interpolator const& interpolator ) const
{
	dispatcher.dispatch		( interpolator, *this );
}

void fermi_dirac_interpolator::visit						( interpolator_comparer& dispatcher, fermi_dirac_interpolator const& interpolator ) const
{
	dispatcher.dispatch		( interpolator, *this );
}