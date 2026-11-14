////////////////////////////////////////////////////////////////////////////
//	Created 	: 24.02.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//	contact storage
////////////////////////////////////////////////////////////////////////////

namespace northstarcpp {
namespace render {
	enum debug_user_id;
} // namespace render
} // namespace northstarcpp

class contact;
struct contact_joint_info;

struct contact_predicate
{
	virtual bool operator() (contact& c) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( contact_predicate )
}; // struct contact_predicate

class contact_storage
{
	physics::vector<contact*> m_contacts;
public:
	contact&	add					( const contact_joint_info& c );
	void		clear				( );
	void		for_each			( contact_predicate &predicate );
				~contact_storage	( )	{ clear( ); }
}; // class contact_storage


void render( contact_storage &contacts, render::debug::renderer& renderer );