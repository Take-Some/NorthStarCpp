////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.02.2008
//	Author		: Kayla Verner
//	Description : joint
////////////////////////////////////////////////////////////////////////////
#ifndef NORTHSTARCPP_PHYSICS_JOINT_H_INCLUDED
#define NORTHSTARCPP_PHYSICS_JOINT_H_INCLUDED

namespace northstarcpp {
namespace render {
	enum debug_user_id;
} // namespace render
} // namespace northstarcpp

namespace northstarcpp {
namespace render {
	struct world;
} // namespace render
} // namespace northstarcpp

class body;
class island;

class joint
{
public:
	virtual	void	connect		( body	*b0, body	*b1 )								=0;
	virtual	void	connect		( body	*b )											=0;
	virtual	void	integrate	( float	time_delta )									=0;
	virtual	void	fill_island ( island	&i )										=0;
	virtual	void	set_anchor  ( const float3	&anchor )								=0;
	virtual	void	get_anchor  ( float3	&anchor )const								=0;
	virtual	void	render		( northstarcpp::render::debug::renderer& renderer ) const	=0;
	virtual			~joint		( ) {}

protected:
	joint	(){}

};

#endif