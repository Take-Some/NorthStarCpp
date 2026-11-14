////////////////////////////////////////////////////////////////////////////
//	Created		: 16.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef I_BI_SPLINE_DATA_H_INCLUDED
#define I_BI_SPLINE_DATA_H_INCLUDED

namespace northstarcpp {
namespace animation {

template<typename Point_type>
struct i_bi_spline_data
{
	virtual void	add_point	(  const std::pair<float, Point_type> &p ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( i_bi_spline_data )
};

} // namespace  animation
} // namespace  northstarcpp


#endif // #ifndef BI_SPLINE_DATA_H_INCLUDED