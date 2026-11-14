////////////////////////////////////////////////////////////////////////////
//	Created		: 30.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "drag_and_drop_behavior.h"

const MString drag_and_drop_behavior::typeName	( "northstarcpp_dragndrop_behavior" );

void* drag_and_drop_behavior::creator()
{
	return CRT_NEW(drag_and_drop_behavior);
}

bool drag_and_drop_behavior::shouldBeUsedFor(MObject &sourceNode, MObject &destinationNode, MPlug &sourcePlug, MPlug &destinationPlug)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourceNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourcePlug );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationPlug );
	return false;
}

MStatus drag_and_drop_behavior::connectAttrToAttr(MPlug &sourcePlug, MPlug &destinationPlug, bool force)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourcePlug );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationPlug );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( force );
	MStatus	status;
	return status;
}

MStatus drag_and_drop_behavior::connectAttrToNode(MPlug &sourcePlug, MObject &destinationNode, bool force)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourcePlug );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( force );
	MStatus	status;
	return status;
}

MStatus drag_and_drop_behavior::connectNodeToAttr(MObject &sourceNode, MPlug &destinationPlug, bool force)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourceNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationPlug );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( force );
	
	MStatus	status;
	return status;
}

MStatus drag_and_drop_behavior::connectNodeToNode(MObject &sourceNode, MObject &destinationNode, bool force)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETER( sourceNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( destinationNode );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( force );
	MStatus	status;
	return status;
}


