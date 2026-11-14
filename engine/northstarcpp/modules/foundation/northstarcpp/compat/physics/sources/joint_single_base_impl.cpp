////////////////////////////////////////////////////////////////////////////
//	Created		: 16.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "joint_single_base_impl.h"

void	joint_single_base_impl::connect( body	*b0, body	*b1 )
{
	joint_base_impl::connect( m_joint, b0, b1 );
}

void	joint_single_base_impl::connect( body	*b )
{
	joint_base_impl::connect( m_joint, b );
}

void	joint_single_base_impl::fill_island( island	&i )
{
	joint_base_impl::fill_island( m_joint, i );
}

void	joint_single_base_impl::render( northstarcpp::render::debug::renderer& renderer )const
{
	joint_base_impl::render( m_joint, renderer );
}

void	joint_single_base_impl::integrate( float	time_delta )
{
	joint_base_impl::integrate( m_joint, time_delta );
}