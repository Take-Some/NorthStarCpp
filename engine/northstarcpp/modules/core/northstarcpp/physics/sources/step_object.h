////////////////////////////////////////////////////////////////////////////
//	Created 	: 03.02.2008
//	Author		: Kayla Verner
//	Description : step_object
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_PHYSICS_STEP_OBJECT_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_STEP_OBJECT_H_INCLUDED


class scheduler;

class step_object
{

public:
	virtual bool	step				( scheduler& w, float time_delta ) = 0;
protected:
					~step_object		( );
					step_object			( ) {};
}; // class step_object

#endif