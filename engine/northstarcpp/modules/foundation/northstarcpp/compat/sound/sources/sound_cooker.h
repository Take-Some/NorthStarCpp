////////////////////////////////////////////////////////////////////////////
//	Created		: 22.04.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include <northstarcpp/compat/resources_cook_classes.h>

#ifndef NORTHSTARCPP_SOUND_SOUND_COOKER_H_INCLUDED
#define NORTHSTARCPP_SOUND_SOUND_COOKER_H_INCLUDED

namespace northstarcpp {
namespace sound {

class sound_world;

class sound_cooker :	public resources::translate_query_cook,
						public boost::noncopyable
{
	typedef resources::translate_query_cook	super;
public:
							sound_cooker			( );
	virtual					~sound_cooker			( ) {}

	virtual	void			translate_query			( resources::query_result & parent );
	virtual void			delete_resource			( resources::unmanaged_resource* res);

private:
			void			on_sub_resources_loaded	( resources::queries_result & data );
}; // class sound_cooker

} // namespace sound
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_SOUND_SOUND_COOKER_H_INCLUDED