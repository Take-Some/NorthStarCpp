////////////////////////////////////////////////////////////////////////////
//	Created		: 08.05.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CREATE_COLLISION_CMD_H_INCLUDED
#define CREATE_COLLISION_CMD_H_INCLUDED

namespace Wm4 {
	template <typename Real>
	class Vector3;
} // namespace Wm4

class createCollisionCmd : public MPxCommand 
{
public:
	static const MString		Name;
public:
	virtual MStatus	doIt ( const MArgList& );
	virtual MStatus undoIt();
 	virtual MStatus redoIt();
	virtual bool isUndoable() const { return true; } 

	static void *creator() { return CRT_NEW(createCollisionCmd); }
	static MSyntax newSyntax();

private:
	MDagModifier dagMod;
private:
	MStatus			auto_calculate_shape( const northstarcpp::maya::vector< Wm4::Vector3<float> >& verts, short type, MObject& depNodeBody );
};

#endif // #ifndef CREATE_COLLISION_CMD_H_INCLUDED