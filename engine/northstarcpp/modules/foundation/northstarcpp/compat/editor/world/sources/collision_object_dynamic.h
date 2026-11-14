////////////////////////////////////////////////////////////////////////////
//	Created		: 23.03.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COLLISION_OBJECT_DYNAMIC_H_INCLUDED
#define COLLISION_OBJECT_DYNAMIC_H_INCLUDED

#include "collision_object.h"

namespace northstarcpp {
namespace editor {
ref class object_base;

class collision_object_dynamic : public collision_object
{
public:
	collision_object_dynamic	(	collision::geometry const* geometry,
									object_base^ dynamic,
									collision::object_type t):
									collision_object	( geometry ),
									m_owner				( dynamic )	{ set_type(t);}

	object_base^							get_owner			( ) const;

private:
	gcroot<object_base^>			m_owner;

}; //collision_object_dynamic

}// namespace editor
}// namespace northstarcpp
#endif // #ifndef COLLISION_OBJECT_DYNAMIC_H_INCLUDED