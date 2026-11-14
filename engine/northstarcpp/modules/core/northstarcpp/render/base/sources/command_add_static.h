////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_ADD_STATIC_H_INCLUDED
#define COMMAND_ADD_STATIC_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <northstarcpp/resources.h>

namespace northstarcpp {
namespace render {

class base_world;
struct platform;

class command_add_static :
	public engine::command,
	private boost::noncopyable
{
public:
							command_add_static	( base_world& world, resources::managed_resource_ptr const& collision, resources::managed_resource_ptr const& geometry, resources::managed_resource_ptr const& visuals );
	virtual	void			execute				( );

private:
	resources::managed_resource_ptr m_collision;
	resources::managed_resource_ptr m_geometry;
	resources::managed_resource_ptr m_visuals;
	base_world&			m_world;
}; // struct command_delegate

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_ADD_STATIC_H_INCLUDED