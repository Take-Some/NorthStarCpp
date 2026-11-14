////////////////////////////////////////////////////////////////////////////
//	Created		: 27.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "joint_slider_hinge.h"
#include "joint_ode.h"
joint_slider_hinge::joint_slider_hinge	( ):
joint_double_base_impl(
					   create_ode_slider( ),
					   create_ode_angular_motor( )
					   )
{
}

void joint_slider_hinge::set_anchor ( const float3	&anchor )		
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( anchor );
}

void joint_slider_hinge::get_anchor ( float3	&anchor )const		
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( anchor );
}

joint_slider_hinge::~joint_slider_hinge	()
{
 	detach( m_joint0 );
	destroy_joint( m_joint0 );

	detach( m_joint1 );
	destroy_joint( m_joint1 );
}