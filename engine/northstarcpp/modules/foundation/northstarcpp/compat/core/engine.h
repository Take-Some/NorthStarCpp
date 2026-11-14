////////////////////////////////////////////////////////////////////////////
//	Created 	: 21.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CORE_ENGINE_H_INCLUDED
#define NORTHSTARCPP_CORE_ENGINE_H_INCLUDED

namespace northstarcpp {
namespace core {

struct NORTHSTARCPP_NOVTABLE engine {
	virtual			~engine					( ) { }
	virtual void 	exit					( int exit_code ) = 0;
	virtual void 	set_exit_code			( int exit_code ) = 0;
	virtual int  	get_exit_code			( ) const = 0;
	virtual	pcstr	get_resource_path		( ) const = 0;
	virtual	pcstr	get_mounts_path			( ) const = 0;
	virtual	pcstr	get_underscore_G_path	( ) const = 0;
}; // class engine

} // namespace core
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CORE_ENGINE_H_INCLUDED