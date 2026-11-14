////////////////////////////////////////////////////////////////////////////
//	Created		: 16.11.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef IACTION_H_INCLUDED
#define IACTION_H_INCLUDED

template< class space_params >
class iaction
{
public:
	virtual				~iaction			( ) { }
	virtual		void	render				( const space_params& a,  northstarcpp::render::debug::renderer& renderer ) const	=0;
};


#endif // #ifndef IACTION_H_INCLUDED