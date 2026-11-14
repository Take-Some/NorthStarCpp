////////////////////////////////////////////////////////////////////////////
//	Created		: 01.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CONFIGS_BINARY_CONFIG_H_INCLUDED
#define NORTHSTARCPP_CONFIGS_BINARY_CONFIG_H_INCLUDED

#include <northstarcpp/compat/configs.h>
#include <northstarcpp/compat/resources.h>
#include <northstarcpp/compat/configs_binary_config_value.h>

namespace northstarcpp {
namespace configs {

class binary_config :
	public detail::noncopyable,
	public resources::unmanaged_resource
{
public:
	typedef binary_config_value::const_iterator			const_iterator;

public:
	virtual								~binary_config	( )	{ }
	inline	binary_config_value const&	get_root		( ) const					{ return *m_root; }
	inline	bool						empty			( ) const					{ return get_root().empty(); }
	inline	u32							size			( ) const					{ return get_root().size(); }
	inline	const_iterator				begin			( ) const					{ return get_root().begin(); }
	inline	const_iterator				end				( ) const					{ return get_root().end(); }
	inline	binary_config_value const&	operator[]		( u32 const index ) const	{ return get_root()[ index ]; }
	inline	binary_config_value const&	operator[]		( int const index ) const	{ return get_root()[ index ]; }
	inline	binary_config_value const&	operator[]		( pcstr const key ) const	{ return get_root()[ key ]; }

protected:
	binary_config_value*				m_root;
}; // class binary_config

typedef	resources::resource_ptr	<
			binary_config, 
			resources::unmanaged_intrusive_base
		>	binary_config_ptr;

} // namespace configs
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CONFIGS_BINARY_CONFIG_H_INCLUDED