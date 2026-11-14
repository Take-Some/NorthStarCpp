////////////////////////////////////////////////////////////////////////////
//	Created		: 23.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COLLISION_STATIC_H_INCLUDED
#define COLLISION_STATIC_H_INCLUDED

#include <northstarcpp/collision/geometry.h>
#include <northstarcpp/physics/shell.h>

namespace stalker2{

class collision_static {

public:
			collision_static	();
	void	create				( northstarcpp::resources::managed_resource_ptr const mesh_resource, physics::world &w  );
	void	destroy				( physics::world &w );
private:
	northstarcpp::collision::geometry*	m_collision;
	northstarcpp::collision::object*	m_collision_object;
	northstarcpp::physics::shell*		m_statics_shell;
}; // class collision_static

} // namespace stalker2

#endif // #ifndef COLLISION_STATIC_H_INCLUDED