////////////////////////////////////////////////////////////////////////////
//	Created		: 09.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "maya_engine.h"
#include <northstarcpp/compat/fs_path.h>
#include <northstarcpp/compat/fs_utils.h>
namespace northstarcpp {
namespace maya{


maya_engine::maya_engine( ): 
animation_world( 0 ), 
maya_animation_world( 0 )
{
	assign_resource_path( );
}

render::world&	maya_engine::get_renderer_world( )	
{
	UNREACHABLE_CODE(return *((render::world*)(0)));
}

void module_path_dir( string_path dir_path )
{
	pcstr module_name = "northstarcpp.maya.mll";
	HMODULE handle = GetModuleHandle( module_name  );
	ASSERT( handle );
	
	GetModuleFileName( handle, dir_path, sizeof( string_path ) );

	fs::path_string path_string = northstarcpp::fs::convert_to_portable( dir_path );

	fs::path_string dir_path_string;
	northstarcpp::fs::directory_part_from_path( &dir_path_string, path_string.c_str() );

	northstarcpp::strings::copy( dir_path, sizeof( string_path ), dir_path_string.c_str() );
}

void	maya_engine::assign_resource_path( )
{
	module_path_dir( m_resource_path );
	northstarcpp::strings::join( m_resource_path, sizeof( m_resource_path ), m_resource_path, "/../../resources/" );
	module_path_dir( m_underscore_G_path );
	northstarcpp::strings::join( m_underscore_G_path, sizeof( m_underscore_G_path ), m_underscore_G_path, "/../../resources/scripts/" );
}

pcstr	maya_engine::get_resource_path( )const			
{ 
	return m_resource_path; 
}

pcstr	maya_engine::get_underscore_G_path( )const
{ 

	return m_underscore_G_path; 
}

} // namespace maya
} // namespace northstarcpp