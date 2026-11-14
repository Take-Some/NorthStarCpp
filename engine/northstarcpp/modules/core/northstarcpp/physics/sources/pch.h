////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.11.2008
//	Author		: Kayla Verner
//	Description : precompiled header
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_PHYSICS_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"physics"
#include <northstarcpp/extensions.h>

#include "physics_memory.h"

using namespace northstarcpp;
//using namespace northstarcpp::physics;
namespace northstarcpp {
namespace physics {

	enum axis
	{
		x_axis = 0,
		y_axis,
		z_axis,
		none_axis	
	};

}
}

using  northstarcpp::physics::axis;
using  northstarcpp::physics::x_axis;
using  northstarcpp::physics::y_axis;
using  northstarcpp::physics::z_axis;
using  northstarcpp::physics::none_axis;
#endif // #ifndef PCH_H_INCLUDED