////////////////////////////////////////////////////////////////////////////
//	Created 	: 16.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DEFAULT_WORLD_H_INCLUDED
#define NORTHSTARCPP_DEFAULT_WORLD_H_INCLUDED
#include <northstarcpp/compat/maya_animation/raw_bi_spline.h>

namespace northstarcpp {

namespace animation {
 struct i_skeleton_animation_data;
} // namespace animation

namespace maya_animation {

class	discrete_data;
struct	handler;

typedef	raw_bi_spline<float,1> raw_bi_spline_1d;
typedef	raw_bi_spline<float3,3> raw_bi_spline_3d;

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void					tick					( ) = 0;
	virtual void					build_animation_data	( const maya_animation::discrete_data &data, animation::i_skeleton_animation_data &animation_data   )const =0 ;
	virtual raw_bi_spline_1d		*create_1d_bi_spline	( )const = 0;
	virtual raw_bi_spline_3d		*create_3d_bi_spline	( )const = 0;
	virtual discrete_data			*create_discrete_data	( )const = 0;
	virtual void					destroy					( raw_bi_spline<float,1>	* &p )const = 0;
	virtual void					destroy					(  raw_bi_spline<float3,3>	* &p )const = 0;
	virtual void					destroy					(  maya_animation::discrete_data	* &p )const = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace maya_animation
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_DEFAULT_WORLD_H_INCLUDED