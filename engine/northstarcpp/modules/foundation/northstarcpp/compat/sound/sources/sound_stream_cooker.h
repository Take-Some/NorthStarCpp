////////////////////////////////////////////////////////////////////////////
//	Created		: 22.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SOUND_STREAM_COOKER_H_INCLUDED
#define SOUND_STREAM_COOKER_H_INCLUDED

#include <northstarcpp/compat/resources_cook_classes.h>

namespace northstarcpp {
namespace sound {

class sound_world;

class sound_stream_cooker :	public resources::translate_query_cook,
							public boost::noncopyable
{
	typedef resources::translate_query_cook	super;
public:
							sound_stream_cooker		( );
	virtual					~sound_stream_cooker	( );

	virtual void			delete_resource			( resources::unmanaged_resource* res);
	virtual void			translate_query			( resources::query_result& parent );

private:
			void			on_sub_resources_loaded	( resources::queries_result& data ); // raw ogg file loaded
			void			on_raw_properties_loaded( resources::queries_result& result );
}; // class sound_stream_cooker


} // namespace sound
} // namespace northstarcpp

#endif // #ifndef SOUND_STREAM_COOKER_H_INCLUDED