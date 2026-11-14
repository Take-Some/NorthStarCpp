////////////////////////////////////////////////////////////////////////////
//	Created 	: 05.02.2008
//	Author		: Kayla Verner
//	Description : element collision interface
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_CONTACT_JOINT_INFO_H_INCLUDED
#define NORTHSTARCPP_CONTACT_JOINT_INFO_H_INCLUDED

#include <northstarcpp/collision/contact_info.h>

struct contact_joint_info
{
	northstarcpp::collision::contact_info		c;
	float								integration_step;
}; // struct contact_joint_info

#endif