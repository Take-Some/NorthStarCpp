////////////////////////////////////////////////////////////////////////////
//	Created		: 31.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SPACE_PARAMS_INLINE_H_INCLUDED
#define SPACE_PARAMS_INLINE_H_INCLUDED

#include "rtp_world.h"


#include <northstarcpp/compat/configs.h>
#include <northstarcpp/compat/math_randoms_generator.h>

namespace northstarcpp {
namespace rtp {


template< u32 dimension, class type >
void	space_param<dimension, type >::set_random	( )
{
	
	for( u32 i = 0; i < dimension; ++i )
	{
		float range = max[i] - min[i];
		params[i] = min[i] + float( random( rtp_world::random() ) * range );
	}

}


template< u32 dimension, class type >
bool	space_param<dimension, type >::similar	( const space_param< dimension, type >& l )const
{
	for( u32 i = 0; i < dimension; ++i )
		if( !math::similar( params[i], l[i] ,math::epsilon_5 ) )
			return false;
	return true;
}

template< u32 dimension, class type >
void	space_param<dimension, type >::save	( northstarcpp::configs::lua_config_value cfg )const
{
	for( u32 i = 0; i < dimension; ++i )
		cfg[i] = params[i];
	
}

template< u32 dimension, class type >
void	space_param<dimension, type >::load	( const northstarcpp::configs::lua_config_value &cfg )
{
	for( u32 i = 0; i < dimension; ++i )
		 params[i] = cfg[i];
}


template< u32 dimension, class type >
inline float space_param<dimension, type >::operator [] ( u32 d ) const
{
	ASSERT( d < dimension );
	return params[ d ];
}

template< u32 dimension, class type >
float &space_param<dimension, type >::operator []	( u32 d )
{
	ASSERT( d < dimension );
	return params[ d ];
}

} // namespace rtp
} // namespace northstarcpp

#endif // #ifndef SPACE_PARAMS_INLINE_H_INCLUDED