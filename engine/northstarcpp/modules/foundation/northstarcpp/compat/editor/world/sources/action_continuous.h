////////////////////////////////////////////////////////////////////////////
//	Created		: 14.05.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GUI_ACTION_H_INCLUDED
#define GUI_ACTION_H_INCLUDED

#include "action.h"
#include "input_action.h"

namespace northstarcpp {
namespace editor {

public ref class action_continuous abstract: action, input_action 
{
protected:
						action_continuous	( System::String^ name ):m_name(name) { ASSERT( m_name != nullptr ); }

	virtual				~action_continuous	( )	{}
public:
	
	virtual bool			enabled			( )	override	{ return true; }

	virtual	System::String^	name			( ) override	{ return m_name; }
	virtual	System::String^	text			( )				{ return name();}
	virtual checked_state	checked_state	( ) 			{ return checked_state::uncheckable;}

private:
	System::String^	m_name;
}; // class action_continuous

} // namespace editor
} // namespace northstarcpp


#endif // #ifndef GUI_ACTION_H_INCLUDED