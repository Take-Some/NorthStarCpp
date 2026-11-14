////////////////////////////////////////////////////////////////////////////
//	Created		: 24.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

namespace northstarcpp {
namespace maya_animation {

inline void write(const northstarcpp::maya_animation::anim_track_discrete_channels &v, FILE* f )
{
	v.save( f );
}

inline void read( northstarcpp::maya_animation::anim_track_discrete_channels &v, FILE* f )
{
	v.load( f );
} 

inline void lua_write( const northstarcpp::maya_animation::anim_track_discrete_channels &ch, northstarcpp::configs::lua_config_value f )
{
	ch.save( f );
}

inline void lua_read(  northstarcpp::maya_animation::anim_track_discrete_channels &ch, northstarcpp::configs::lua_config_value const& f )
{
	ch.load( f );
}

} // namespace maya_animation
} // namespace northstarcpp