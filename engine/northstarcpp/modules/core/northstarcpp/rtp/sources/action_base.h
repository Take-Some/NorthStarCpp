////////////////////////////////////////////////////////////////////////////
//	Created		: 03.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef ACTION_BASE_H_INCLUDED
#define ACTION_BASE_H_INCLUDED

#include "action.h"

namespace northstarcpp {
namespace rtp {



template< class action >
class action_base : private boost::noncopyable {

public:
	typedef action	action_type;

public:
		action_base ( const global_actions_params< action > &g_par );
		~action_base( );

public:

		u32		add			( action* a	);
const	action*	get			( u32 id ) const ;
		u32		size		( ) const	{ return actions.size();}

public:
void			save		( northstarcpp::configs::lua_config_value cfg )const;
void			load		( const northstarcpp::configs::lua_config_value &cfg );

global_actions_params< action >&		global_params()			{ return m_global_params; }
const global_actions_params< action >&	global_params()const	{ return m_global_params; }

private:
void			clear		();

private:
	rtp::vector<action*>			actions;

private:
	global_actions_params< action > m_global_params;

}; // class action_base

} // namespace rtp
} // namespace northstarcpp

#include "action_base_inline.h"

#endif // #ifndef ACTION_BASE_H_INCLUDED