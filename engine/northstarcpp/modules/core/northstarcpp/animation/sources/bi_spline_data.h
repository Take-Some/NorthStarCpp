////////////////////////////////////////////////////////////////////////////
//	Created		: 18.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef BI_SPLINE_DATA_H_INCLUDED
#define BI_SPLINE_DATA_H_INCLUDED
#include <northstarcpp/animation/i_bi_spline_data.h>

namespace northstarcpp {

namespace configs{
class lua_config_value;
} // namespace configs

namespace animation {

template< typename Point_type >
class bi_spline_data:
	public i_bi_spline_data< Point_type >
{

public:
virtual			void				add_point( const std::pair< float, Point_type > &p  ){ data.push_back( p ); }

public:
				void				save			( northstarcpp::configs::lua_config_value config  )const ;
				void				load			( northstarcpp::configs::lua_config_value const& config  );

public:
		inline  u32					points_number	( )		 const  { return data.size(); }
		inline	const Point_type	point			( u32 i )const	{ return data[i].second; }
		inline  float				knot			( u32 i )const ;
				bool				compare			( const bi_spline_data & ch_d )const;

private:
	vector< std::pair< float, Point_type > >	data;

	static const	u32		bi_spline_order	= 4;

}; // class bi_spline_data

}  // namespace animation
}  // namespace northstarcpp

#include "bi_spline_data_inline.h"
#endif // #ifndef BI_SPLINE_DATA_H_INCLUDED