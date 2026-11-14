////////////////////////////////////////////////////////////////////////////
//	Created		: 18.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RAW_BI_SPLINE_H_INCLUDED
#define RAW_BI_SPLINE_H_INCLUDED

#include <northstarcpp/compat/animation/i_bi_spline_data.h>

namespace northstarcpp {
namespace maya_animation {

template<typename Point_type, int Dimension>
class raw_bi_spline {
public:
	virtual void evaluate			( float time, Point_type &point )const								= 0;
	virtual void get_data			( animation::i_bi_spline_data<Point_type> &data )const				= 0;
	virtual void build_approximation( const float* points, u32 number, const Point_type &max_epsilon  )	= 0;
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( raw_bi_spline )
}; // class raw_bi_spline

} // namespace maya_animation
} // namespace northstarcpp
#endif // #ifndef RAW_BI_SPLINE_H_INCLUDED