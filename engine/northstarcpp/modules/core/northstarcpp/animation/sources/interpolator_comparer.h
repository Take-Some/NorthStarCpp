////////////////////////////////////////////////////////////////////////////
//	Created		: 02.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INTERPOLATOR_COMPARER_H_INCLUDED
#define INTERPOLATOR_COMPARER_H_INCLUDED

namespace northstarcpp {
namespace animation {

class instant_interpolator;
class linear_interpolator;
class fermi_dirac_interpolator;

class interpolator_comparer {
public:
	enum enum_result {
		equal,
		less,
		more,
	}; // enum enum_result

public:
	void	dispatch	( instant_interpolator const& left, instant_interpolator const& right );
	void	dispatch	( instant_interpolator const& left, linear_interpolator const& right );
	void	dispatch	( instant_interpolator const& left, fermi_dirac_interpolator const& right );

	void	dispatch	( linear_interpolator const& left, instant_interpolator const& right );
	void	dispatch	( linear_interpolator const& left, linear_interpolator const& right );
	void	dispatch	( linear_interpolator const& left, fermi_dirac_interpolator const& right );

	void	dispatch	( fermi_dirac_interpolator const& left, instant_interpolator const& right );
	void	dispatch	( fermi_dirac_interpolator const& left, linear_interpolator const& right );
	void	dispatch	( fermi_dirac_interpolator const& left, fermi_dirac_interpolator const& right );

public:
	enum_result			result;
}; // class interpolator_comparer

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef INTERPOLATOR_COMPARER_H_INCLUDED