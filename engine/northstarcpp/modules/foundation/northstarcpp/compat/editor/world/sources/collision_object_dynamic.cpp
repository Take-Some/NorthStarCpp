////////////////////////////////////////////////////////////////////////////
//	Created		: 23.03.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "collision_object_dynamic.h"
#include "object_base.h"

using northstarcpp::editor::object_base;
using northstarcpp::editor::collision_object_dynamic;

object_base^ collision_object_dynamic::get_owner( ) const 
{ 
	return m_owner; 
}
