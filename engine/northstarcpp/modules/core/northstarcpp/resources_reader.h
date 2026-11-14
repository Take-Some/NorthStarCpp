////////////////////////////////////////////////////////////////////////////
//	Created 	: 20.12.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RESOURCES_READER_H_INCLUDED
#define NORTHSTARCPP_RESOURCES_READER_H_INCLUDED

namespace northstarcpp {
namespace resources {

template < typename base_class >
class reader : public base_class {
public:
	inline	reader					( managed_resource_ptr const& resource ) :
		base_class			( get_data( resource ), resource->get_size( ) ),
		m_resource			( resource )
	{
	}

	inline	~reader					( )
	{
		m_resource->unpin	( const_cast<pbyte>( base_class::data( ) ) );
	}

private:
	static inline pcbyte get_data	( managed_resource_ptr const& resource )
	{
		ASSERT				( resource );
		return				( resource->pin	( ) );
	}

private:
	managed_resource_ptr	m_resource;
}; // class reader

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RESOURCES_READER_H_INCLUDED