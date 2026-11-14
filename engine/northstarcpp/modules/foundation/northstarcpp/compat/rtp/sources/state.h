////////////////////////////////////////////////////////////////////////////
//	Created		: 03.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

class action;
class space_params;

class state {
public:
	virtual	action*			action			( )	= 0;
	virtual	space_params*	space_params	( )	= 0;

protected:
	virtual					~state			( )	= 0;
}; // class state

#endif // #ifndef STATE_H_INCLUDED