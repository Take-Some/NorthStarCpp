////////////////////////////////////////////////////////////////////////////
//	Created		: 29.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INTERPOLATOR_VISITOR_H_INCLUDED
#define INTERPOLATOR_VISITOR_H_INCLUDED

namespace northstarcpp {
namespace animation {

class instant_interpolator;
class linear_interpolator;
class fermi_dirac_interpolator;

struct NORTHSTARCPP_NOVTABLE interpolator_visitor {
	virtual	bool	visit			( instant_interpolator& other ) = 0;
	virtual	bool	visit			( instant_interpolator const& other ) const = 0;
	virtual	bool	visit			( linear_interpolator const& other ) const = 0;
	virtual	bool	visit			( fermi_dirac_interpolator const& other ) const = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( interpolator_visitor )
}; // class interpolator_visitor

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef INTERPOLATOR_VISITOR_H_INCLUDED