////////////////////////////////////////////////////////////////////////////
//	Created		: 08.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "check_animation_data.h"
#include "skeleton_animation_data.h"
#include "skeleton_animation.h"
#include <northstarcpp/animation/world.h>

#if NORTHSTARCPP_USE_MAYA_ANIMATION
#	include <northstarcpp/maya_animation/world.h>
#	include <northstarcpp/maya_animation/api.h>
#	include <northstarcpp/maya_animation/discrete_data.h>
#endif // #if NORTHSTARCPP_USE_MAYA_ANIMATION

namespace northstarcpp {

namespace maya_animation
{
	class discrete_data;
	struct world;
} // namespace maya_animation

namespace animation {

bool	compare(	const northstarcpp::maya_animation::discrete_data &disc_data, 
					const skeleton_animation &anim )
{

	NORTHSTARCPP_UNREFERENCED_PARAMETER( anim );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( disc_data );

#if NORTHSTARCPP_USE_MAYA_ANIMATION
	const u32 num_bones = disc_data.bone_count();
	if( anim.bone_count() != num_bones ) 
		return false;
	
	const u32 frame_count = disc_data.frame_count();

	for( u32 bone = 0; bone < num_bones; ++bone )
	{

			for(u32 ch = channel_translation_x; ch  < channel_max; ++ch )
			{
				for(u32 frame_number = 0; frame_number < frame_count; ++frame_number )	
				{
					northstarcpp::animation::frame frame; float time = northstarcpp::math::QNaN;
					disc_data.get_frame( bone, frame_number, time, frame ) ;

					float v = northstarcpp::math::QNaN;
					anim.bone( bone ).channel( enum_channel_id(ch) ).evaluate( time, v );
				
					if(! northstarcpp::math::similar(v,frame.channels[ch],disc_data.precision( bone, enum_channel_id(ch) ) ) )
						return false;
				}
			}
	}
#endif // #if NORTHSTARCPP_USE_MAYA_ANIMATION

	return true;
}

void	test_data(	northstarcpp::configs::lua_config_value	const &animation_data, 
					northstarcpp::configs::lua_config_value	const &check_data,
					animation::world &world
								)
{

	NORTHSTARCPP_UNREFERENCED_PARAMETER( animation_data );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( check_data );
	NORTHSTARCPP_UNREFERENCED_PARAMETER( world );

#if NORTHSTARCPP_USE_MAYA_ANIMATION
	northstarcpp::maya_animation::engine* meng(0);
	northstarcpp::maya_animation::memory_allocator( *northstarcpp::animation::g_allocator );
	northstarcpp::maya_animation::world *maw = northstarcpp::maya_animation::create_world( *meng );
	ASSERT( maw );

	northstarcpp::maya_animation::discrete_data *disc_data = maw->create_discrete_data();

	disc_data->load( check_data["discrete_data"] );

	northstarcpp::animation::i_skeleton_animation_data *ianim_data_builded	= world.create_skeleton_animation_data( 0 );
	northstarcpp::animation::i_skeleton_animation_data *ianim_data_loaded	= world.create_skeleton_animation_data( 0 );
	
	northstarcpp::animation::skeleton_animation_data *anim_data_builded =  static_cast_checked<northstarcpp::animation::skeleton_animation_data*> ( ianim_data_builded  );
	northstarcpp::animation::skeleton_animation_data *anim_data_loaded =  static_cast_checked<northstarcpp::animation::skeleton_animation_data*> ( ianim_data_loaded );

	maw->build_animation_data( *disc_data, *anim_data_builded );

	anim_data_loaded->load( animation_data["splines"] );

	vector< u32 > identity_index;
	const u32 bone_count = anim_data_loaded->num_bones();
	identity_index.resize( bone_count );
	for( u32 bone = 0 ; bone < bone_count ; ++bone )
		identity_index[bone] = bone;


	skeleton_animation *anim_builded = NEW(skeleton_animation)( *anim_data_builded, identity_index );
	ASSERT( compare( *disc_data, *anim_builded ) );
	skeleton_animation *anim_loaded = NEW(skeleton_animation)( *anim_data_loaded, identity_index );
	ASSERT( compare( *disc_data, *anim_loaded ) );

	ASSERT( anim_data_builded->compare( *anim_data_loaded ) );
	
	maw->destroy( disc_data );
	world.destroy( ianim_data_builded );
	world.destroy( ianim_data_loaded );
	DELETE( anim_builded );
	DELETE( anim_loaded );
	northstarcpp::maya_animation::destroy_world( maw );
#endif // #if NORTHSTARCPP_USE_MAYA_ANIMATION
}

} // namespace animation
} // namespace northstarcpp
