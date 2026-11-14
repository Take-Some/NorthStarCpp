////////////////////////////////////////////////////////////////////////////
//	Created		: 06.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

namespace northstarcpp {
namespace rtp {

template< class space_params, class world_space_params, class action  >
abstract_action<space_params, world_space_params, action>::abstract_action( northstarcpp::rtp::action_base<action_type> &ab )
:m_id(u32(-1)),
m_action_base(ab)
{
	m_id = m_action_base.add(static_cast<action_type*>(this));
}

template< class space_params, class world_space_params, class action  >
bool	abstract_action<space_params, world_space_params, action>::is_my_base	( const  action_base<action_type> &ab )const	
{
	return &m_action_base == &ab && m_action_base.get( id() ) == this;
}

template< class space_params, class world_space_params, class action  >
inline void	abstract_action<space_params, world_space_params, action>::save				( northstarcpp::configs::lua_config_value cfg )const
{
	cfg["id"] = m_id;
}

template< class space_params, class world_space_params, class action  >
inline void	abstract_action<space_params, world_space_params, action>::load				( const northstarcpp::configs::lua_config_value &cfg )
{
	m_id = cfg["id"];
}

} // namespace rtp
} // namespace northstarcpp