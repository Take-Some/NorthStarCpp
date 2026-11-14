////////////////////////////////////////////////////////////////////////////
//	Created		: 15.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SIMPLE_ENGINE_H_INCLUDED
#define SIMPLE_ENGINE_H_INCLUDED

#include <northstarcpp/core/engine.h>

namespace northstarcpp {
namespace core {

class simple_engine : public engine {
public:
	virtual void	exit					( int error_code )	{ m_exit_code = error_code; northstarcpp::debug::terminate(""); }
	virtual int		get_exit_code			( ) const			{ return m_exit_code; }
	virtual void	set_exit_code			( int error_code )	{ m_exit_code = error_code; }
	virtual	pcstr	get_resource_path		( ) const			{ return "../../resources"; }
	virtual	pcstr	get_mounts_path			( ) const			{ return NULL; }
	virtual	pcstr	get_underscore_G_path	( ) const			{ return "../../resources/scripts"; }

private:
	int				m_exit_code;
}; // class simple_engine

} // namespace core
} // namespace northstarcpp

#endif // #ifndef SIMPLE_ENGINE_H_INCLUDED