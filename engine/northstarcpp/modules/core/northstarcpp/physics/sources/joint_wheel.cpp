////////////////////////////////////////////////////////////////////////////
//	Created		: 27.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "joint_wheel.h"
#include "joint_ode.h"
joint_wheel::joint_wheel():joint_single_base_impl( create_ode_hinge2() )
{

}

joint_wheel::~joint_wheel()
{
	detach( m_joint );
	destroy_joint( m_joint );
}

void joint_wheel::set_anchor ( const float3	&anchor )		
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( anchor );
}

void joint_wheel::get_anchor ( float3	&anchor )const		
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER	( anchor );
}

