////////////////////////////////////////////////////////////////////////////
//	Created		: 10.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TRIANGLE_BOX_RES_H_INCLUDED
#define TRIANGLE_BOX_RES_H_INCLUDED
#include "triangle_mesh_primitiv_res.h"
namespace northstarcpp {
namespace collision {
struct on_contact;
namespace detail{
struct triangle_box_res: 
public triangle_mesh_primitiv_res
{

	triangle_box_res( u32 _local_id, const u32 *ind ): triangle_mesh_primitiv_res( _local_id, ind ){}

}; // class triangle_box_res
}// northstarcpp
}// collision
}// detail
#endif // #ifndef TRIANGLE_BOX_RES_H_INCLUDED