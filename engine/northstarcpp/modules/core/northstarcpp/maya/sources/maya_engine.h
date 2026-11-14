////////////////////////////////////////////////////////////////////////////
//	Created		: 29.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MAYA_ENGINE_H_INCLUDED
#define MAYA_ENGINE_H_INCLUDED
#include <northstarcpp/core/simple_engine.h>
#include <northstarcpp/maya_animation/engine.h>
#include <northstarcpp/animation/engine.h>

namespace northstarcpp {

namespace animation {
	struct world;
}

namespace maya_animation {
	struct world;
}

namespace maya{

class maya_engine :
	public northstarcpp::core::engine,
	public northstarcpp::animation::engine,
	public northstarcpp::maya_animation::engine
{
public:
	maya_engine			( );

	virtual void			exit						( int error_code )	{ m_exit_code = error_code; northstarcpp::debug::terminate(""); }
	virtual int				get_exit_code				( ) const			{ return m_exit_code; }
	virtual void			set_exit_code				( int error_code )	{ m_exit_code = error_code; }
	virtual	pcstr			get_resource_path			( ) const			;
	virtual	pcstr			get_mounts_path				( ) const			{ return 0; }
	virtual	pcstr			get_underscore_G_path		( ) const			;
	virtual	render::world&	get_renderer_world			( )					;
	
	northstarcpp::animation::world * animation_world;
	northstarcpp::maya_animation::world * maya_animation_world;
public:
	void			assign_resource_path		( );
private:
	
	int				m_exit_code;

	string_path		m_resource_path;
	string_path		m_underscore_G_path;
} ; // class simple_engine
#endif // #ifndef MAYA_ENGINE_H_INCLUDED

extern	maya_engine	&g_maya_engine;
}
}