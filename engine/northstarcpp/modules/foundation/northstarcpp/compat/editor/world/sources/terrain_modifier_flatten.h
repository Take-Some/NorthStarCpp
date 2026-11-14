////////////////////////////////////////////////////////////////////////////
//	Created		: 26.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_MODIFIER_FLATTEN_H_INCLUDED
#define TERRAIN_MODIFIER_FLATTEN_H_INCLUDED

#include "terrain_modifier_control.h"

namespace northstarcpp {
namespace editor {

public ref class terrain_modifier_flatten : public terrain_modifier_height
{
	typedef terrain_modifier_height	super;
public:
								terrain_modifier_flatten( level_editor^ le, tool_terrain^ tool );
	virtual						~terrain_modifier_flatten( );
	virtual		void			execute_input			( ) override;
	virtual		void			do_work						() override;

}; // class terrain_modifier_flatten

} // namespace editor
} // namespace northstarcpp


#endif // #ifndef TERRAIN_MODIFIER_FLATTEN_H_INCLUDED