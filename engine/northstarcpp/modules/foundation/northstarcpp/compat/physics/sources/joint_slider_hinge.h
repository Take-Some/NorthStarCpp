////////////////////////////////////////////////////////////////////////////
//	Created		: 27.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef JOINT_SLIDER_HINGE_H_INCLUDED
#define JOINT_SLIDER_HINGE_H_INCLUDED
#include "joint_double_base_impl.h"


class joint_slider_hinge:
	public joint_double_base_impl
{

	inline	dxJoint*		slider			( )		{ return m_joint0; }
	inline	dxJoint*		angular_motor	( )		{ return m_joint1; }
	inline	const dxJoint*	slider			( )const{ return m_joint0; }
	inline	const dxJoint*	angular_motor	( )const{ return m_joint1; }

public:
					joint_slider_hinge	();
	virtual			~joint_slider_hinge	();

	virtual	void	set_anchor  ( const float3	&anchor );
	virtual	void	get_anchor  ( float3	&anchor )const;

}; // class joint_slider_hinge

#endif // #ifndef JOINT_SLIDER_HINGE_H_INCLUDED