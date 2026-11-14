////////////////////////////////////////////////////////////////////////////
//	Created 	: 02.02.2008
//	Author		: Kayla Verner
//	Description : space_object
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_PHYSICS_SPACE_OBJECT_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_SPACE_OBJECT_H_INCLUDED

namespace northstarcpp {
namespace render {
	enum debug_user_id;
} // namespace render
} // namespace northstarcpp

namespace northstarcpp {

namespace collision {
	struct space_partitioning_tree;
} // namespace collision

namespace render{
	struct world;
} // namespace base

namespace physics {
	class space_item;
} // namespace physics

} // namespace northstarcpp


class island;
class collision_space;
class collide_element_interface;

class	space_object
{
public:
	
			void			space_insert		( northstarcpp::collision::space_partitioning_tree & space );
			void			space_remove		( );

public:
	virtual	void			collide				( island& i, space_object& o )												= 0;

	virtual	bool			collide_detail		( island& i, space_object& o )												= 0;
	virtual	bool			collide				( island& i, collide_element_interface& e )									= 0;
	virtual	void			aabb				( float3 &center, float3 &radius )const										= 0;
			void			move				( )																			;
	virtual	bool			aabb_test			( math::aabb const& aabb ) const											= 0;
	virtual	void			render				( render::debug::renderer& renderer )const							= 0;

private:
	virtual	void			space_check_validity( )const																	= 0;

public:
	inline	northstarcpp::physics::space_item* get_space_item		( )			{ return m_space_item; }
	inline	northstarcpp::physics::space_item const* get_space_item	( ) const	{ return m_space_item; }

protected:
	inline					space_object		( ) : m_space_item( 0 ) { }
							~space_object		( );
private:
	northstarcpp::physics::space_item*	m_space_item;

}; // class	space_object

#endif