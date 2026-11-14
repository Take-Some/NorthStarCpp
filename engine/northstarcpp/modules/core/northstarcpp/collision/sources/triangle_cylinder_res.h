////////////////////////////////////////////////////////////////////////////
//	Created		: 13.03.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TRIANGLE_CYLINDER_RES_H_INCLUDED
#define TRIANGLE_CYLINDER_RES_H_INCLUDED
#include "triangle_mesh_primitiv_res.h"
namespace northstarcpp {
namespace collision {
struct on_contact;
namespace detail{

struct triangle_cylinder_res: 
public triangle_mesh_primitiv_res
{

	triangle_cylinder_res( u32 _local_id, const u32 *ind ): triangle_mesh_primitiv_res( _local_id, ind ){}

}; // class triangle_box_res

}//detail {
}//collision{
}//northstarcpp{
#endif // #ifndef TRIANGLE_CYLINDER_RES_H_INCLUDED