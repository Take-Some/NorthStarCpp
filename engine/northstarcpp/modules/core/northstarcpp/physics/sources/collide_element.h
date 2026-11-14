////////////////////////////////////////////////////////////////////////////
//	Created 	: 02.02.2008
//	Author		: Kayla Verner
//	Description : collision element 
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_PHYSICS_COLLIDE_ELEMENT_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_COLLIDE_ELEMENT_H_INCLUDED

#include "shell_element.h"

namespace northstarcpp {
namespace collision {
	struct on_contact;
	struct geometry_instance;
} // namespace collision
} // namespace northstarcpp

class island;
class collide_element;
class collide_element_iterface;
class body;

class collide_element:
	public shell_element
{
protected:
	collision::geometry_instance	&m_geometry_instance;

protected:
	virtual	void	generate_contacts	( northstarcpp::collision::on_contact& c, const northstarcpp::collision::geometry_instance& og  )const	;

protected:
					~collide_element	();
					collide_element		( northstarcpp::collision::geometry_instance	&cf ):	m_geometry_instance( cf )			{ }
private:
	collide_element& operator =			( collide_element const& )		{ UNREACHABLE_CODE(return *this); }

private:
	virtual	void								render					( render::debug::renderer& renderer ) const;
	virtual	northstarcpp::collision::geometry_instance	&get_geometry			( )													{ return m_geometry_instance; }
	virtual void								check_collision_validity( )const;
	virtual	math::aabb							&aabb					( math::aabb &box ) const;
	virtual	bool								aabb_test				( math::aabb const& aabb )const;
};


#endif