////////////////////////////////////////////////////////////////////////////
//	Created		: 04.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef I_BONE_NAMES_H_INCLUDED
#define I_BONE_NAMES_H_INCLUDED

#include <northstarcpp/animation/type_definitions.h>

namespace northstarcpp {

namespace configs{
	class lua_config_value;
}

namespace animation {

class i_bone_names {

public:
	virtual	void			set_bones_number( bone_index_type size )							=0;
	virtual void			set_name		( bone_index_type bone_index, pcstr name )			=0;
	
public:
	virtual void			read			( northstarcpp::configs::lua_config_value const &cfg )		=0;
	virtual void			write			( northstarcpp::configs::lua_config_value		&cfg )const	=0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( i_bone_names )
};

} // namespace animation
} // namespace northstarcpp

#endif // #ifndef I_BONE_NAMES_H_INCLUDED