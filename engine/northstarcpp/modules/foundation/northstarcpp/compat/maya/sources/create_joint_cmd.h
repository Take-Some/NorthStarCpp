////////////////////////////////////////////////////////////////////////////
//	Created		: 12.05.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CREATE_JOINT_CMD_H_INCLUDED
#define CREATE_JOINT_CMD_H_INCLUDED

class createJointCmd : public MPxCommand 
{
public:
	static const MString		Name;
	virtual						~createJointCmd	()	{};
public:
	virtual MStatus	doIt ( const MArgList& );
	virtual MStatus undoIt();
 	virtual MStatus redoIt();
	virtual bool isUndoable() const { return true; } 

	static void *creator() { return CRT_NEW(createJointCmd); }
	static MSyntax newSyntax();


private:
	MDagModifier dagMod;

private:

}; // class create_joint_cmd

#endif // #ifndef CREATE_JOINT_CMD_H_INCLUDED