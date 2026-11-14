////////////////////////////////////////////////////////////////////////////
//	Created 	: 02.02.2008
//	Author		: Kayla Verner
//	Description : space_object
////////////////////////////////////////////////////////////////////////////
#include "pch.h"
#include "space_object.h"
#include "space_item.h"

void	space_object::space_insert( northstarcpp::collision::space_partitioning_tree & space )
{
	ASSERT( !get_space_item() );

	space_check_validity();

	m_space_item = NEW(northstarcpp::physics::space_item)(*this);
	m_space_item->space_insert( space );
}

void	space_object::space_remove( )
{
	ASSERT( get_space_item() );
	m_space_item->space_remove();
}

space_object::~space_object( )
{
	DELETE( m_space_item );
}

void space_object::move( )
{
	ASSERT( get_space_item() );
	get_space_item()->on_move( );
}